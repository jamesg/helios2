#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <microhttpd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "imageutils_nowarnings.hpp"

#include "slide.hpp"
#include "webserver.hpp"

/*
 * Declare the variables required to access a web resource.
 */
#define WEB_DECLARE_STATIC(NAME) \
    extern "C" {\
        extern const char _binary_##NAME##_start;\
        extern const char _binary_##NAME##_end;\
        extern const size_t _binary_##NAME##_size;\
    }

#define WEB_STATIC_STD_STRING(NAME) \
    std::string(&_binary_##NAME##_start, &_binary_##NAME##_end)

WEB_DECLARE_STATIC(web_404_html)
WEB_DECLARE_STATIC(web_albums_html)
WEB_DECLARE_STATIC(web_application_js)
WEB_DECLARE_STATIC(web_backbone_js)
WEB_DECLARE_STATIC(web_grid_css)
WEB_DECLARE_STATIC(web_index_html)
WEB_DECLARE_STATIC(web_jquery_js)
WEB_DECLARE_STATIC(web_modal_css)
WEB_DECLARE_STATIC(web_modal_js)
WEB_DECLARE_STATIC(web_photograph_html)
WEB_DECLARE_STATIC(web_style_css)
WEB_DECLARE_STATIC(web_teletype_theme_css)
WEB_DECLARE_STATIC(web_underscore_js)
WEB_DECLARE_STATIC(web_views_js)

namespace
{
    std::string g_db_path = "";
    static bool g_db_path_set = false;

    void set_db_path(const std::string& db_path)
    {
        if(g_db_path_set)
            return;
        g_db_path = db_path;
        g_db_path_set = true;
    }

    slide::connection& database()
    {
        if(!g_db_path_set)
            throw std::runtime_error("db path not set");

        static thread_local std::unique_ptr<slide::connection> g_conn;
        if(!g_conn)
            try
            {
                //std::cerr << "open db connection" << std::endl;
                g_conn.reset(new slide::connection(g_db_path));
            }
            catch(const std::exception&)
            {
                std::cerr << "warning: opening database connection with file " << g_db_path << std::endl;
            }
        return *g_conn;
    }

    void create_db()
    {
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_photograph ( "
                " photograph_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                " title VARCHAR NOT NULL, "
                " caption VARCHAR NULL, "
                " taken VARCHAR NULL "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_photograph_location ( "
                " photograph_id INTEGER PRIMARY KEY, "
                " location VARCHAR, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_jpeg_data ( "
                " photograph_id INTEGER PRIMARY KEY, "
                " data BLOB NOT NULL, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_jpeg_small ( "
                " photograph_id INTEGER PRIMARY KEY, "
                " data BLOB NOT NULL, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_jpeg_medium ( "
                " photograph_id INTEGER PRIMARY KEY, "
                " data BLOB NOT NULL, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_album ( "
                " album_id INTEGER PRIMARY KEY AUTOINCREMENT, "
                " name VARCHAR NOT NULL, "
                " caption VARCHAR NULL, "
                " UNIQUE(name) "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_photograph_tagged ( "
                " photograph_id INTEGER NOT NULL, "
                " tag VARCHAR NOT NULL, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED, "
                " UNIQUE(photograph_id, tag) "
                " ) ",
                database()
                );
        slide::devoid(
                "CREATE TABLE IF NOT EXISTS helios_photograph_in_album ( "
                " photograph_id INTEGER NOT NULL, "
                " album_id INTEGER NOT NULL, "
                " FOREIGN KEY(photograph_id) REFERENCES helios_photograph(photograph_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED, "
                " FOREIGN KEY(album_id) REFERENCES helios_album(album_id) "
                "  ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED, "
                " UNIQUE(photograph_id, album_id) "
                " ) ",
                database()
                );
    }

    bool has_jpeg(const int photograph_id, const std::string& table)
    {
        return slide::get_collection<int>(
                database(),
                (slide::mkstr() << "SELECT photograph_id FROM " << table <<
                    " WHERE photograph_id = ?").str().c_str(),
                slide::row<int>::make_row(photograph_id)
                ).size() > 0;
    }
    std::vector<unsigned char> get_fullsize_jpeg(const int photograph_id)
    {
        std::cerr << "get_fullsize_jpeg " << photograph_id << std::endl;
        sqlite3_stmt *stmt;
        sqlite3_prepare(
                database().handle(),
                "SELECT data FROM helios_jpeg_data WHERE photograph_id = ?",
                -1,
                &stmt,
                nullptr
                );
        sqlite3_bind_int(stmt, 1, photograph_id);
        if(slide::step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("retrieving fullsize");
        }
        std::vector<unsigned char> out(
                reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0)),
                reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0)) + sqlite3_column_bytes(stmt, 0)
                );
        sqlite3_finalize(stmt);
        return out;
    }


    void cache_jpeg(
            const int photograph_id,
            const std::string& table,
            const int width,
            const int height
            )
    {
        std::vector<unsigned char> fullsize = get_fullsize_jpeg(photograph_id);
        Magick::Blob out;
        Magick::Image image(Magick::Blob(
            reinterpret_cast<const void*>(&(fullsize[0])), fullsize.size())
            );
        long orientation = 1;
        try
        {   // Retrieve orientation
            auto exiv_image = Exiv2::ImageFactory::open(
                reinterpret_cast<const unsigned char*>(&(fullsize[0])),
                static_cast<long>(fullsize.size())
                );
            exiv_image->readMetadata();

            Exiv2::ExifKey key("Exif.Image.Orientation");
            Exiv2::ExifData::iterator pos = exiv_image->exifData().findKey(key);

            if(pos != exiv_image->exifData().end())
                orientation = pos->getValue()->toLong();
        }
        catch(const std::exception&)
        {
            // Some images don't have an orientation.
        }

        // Scale the image
        // Swap width and height because the image is about to be rotated
        // Scale to fit within this box
        image.scale(
                Magick::Geometry(
                    (orientation == 6 || orientation == 8) ?
                        (slide::mkstr() << height << "x" << width) :
                        (slide::mkstr() << width << "x" << height)
                    )
                );

        // Rotate the image
        switch(orientation)
        {
            case 3:
                image.rotate(180);
            case 6:
                image.rotate(90);
            case 8:
                image.rotate(270);
        }


        // Save the new image.
        Magick::Image out_image(image.size(), Magick::Color(255,255,255));
        out_image.composite(image, 0, 0);
        out_image.write(&out, "JPEG");
        sqlite3_stmt *stmt;
        sqlite3_prepare(
                database().handle(),
                (slide::mkstr() << "INSERT INTO " << table << "(photograph_id, data) "
                "VALUES(?, ?)").str().c_str(),
                -1,
                &stmt,
                nullptr
                );
        sqlite3_bind_int(stmt, 1, photograph_id);
        if(
                // TODO does SQLite need to copy all this data?
                sqlite3_bind_blob(
                    stmt, 2, out.data(),
                    static_cast<int>(out.length()), SQLITE_TRANSIENT
                    ) != SQLITE_OK
                )
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("image");
        }
        slide::step(stmt);
        sqlite3_finalize(stmt);
    }

    std::vector<unsigned char> get_jpeg(const int photograph_id, const std::string& table)
    {
        sqlite3_stmt *stmt;
        sqlite3_prepare(
                database().handle(),
                (slide::mkstr() << "SELECT data FROM " << table <<
                    " WHERE photograph_id = ?").str().c_str(),
                -1,
                &stmt,
                nullptr
                );
        sqlite3_bind_int(stmt, 1, photograph_id);
        if(slide::step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("retrieving jpeg data");
        }
        std::vector<unsigned char> out(
                reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0)),
                reinterpret_cast<const unsigned char*>(sqlite3_column_blob(stmt, 0)) + sqlite3_column_bytes(stmt, 0)
                );
        sqlite3_finalize(stmt);
        return out;
    }

    std::vector<unsigned char> get_small_jpeg(const int photograph_id)
    {
        if(!has_jpeg(photograph_id, "helios_jpeg_small"))
            cache_jpeg(photograph_id, "helios_jpeg_small", 300, 200);

        return get_jpeg(photograph_id, "helios_jpeg_small");
    }

    std::vector<unsigned char> get_medium_jpeg(const int photograph_id)
    {
        if(!has_jpeg(photograph_id, "helios_jpeg_medium"))
            cache_jpeg(photograph_id, "helios_jpeg_medium", 960, 640);

        return get_jpeg(photograph_id, "helios_jpeg_medium");
    }

    int postdata_iterator(
            void *cls,
            enum MHD_ValueKind kind,
            const char *key,
            const char *filename,
            const char *content_type,
            const char *transfer_encoding,
            const char *data,
            uint64_t off,
            size_t size
            );

    class upload_function : public webserver::request_function
    {
        public:
            /*
             * Allow a match only when the URL and method match exactly.
             */
            int match_strength(const char *url, const char *method) override
            {
                return (std::string(url) == "/upload" && std::string(method) == "POST") ? 1 : -1;
            }

            struct connection_status
            {
                MHD_PostProcessor *post_processor;
                std::string title, caption, location;
                std::vector<unsigned char> jpeg_data;
                std::size_t data_size;

                connection_status() :
                    post_processor(nullptr),
                    data_size(0)
                {
                }
            };

            int operator()(
                    void */*cls*/,
                    struct MHD_Connection *connection,
                    const char */*url*/,
                    const char */*method*/,
                    const char */*version*/,
                    const char *upload_data,
                    size_t *upload_data_size,
                    void **con_cls
                    ) override
            {
                connection_status *con = nullptr;

                if(*con_cls == nullptr)
                {
                    // New connection.
                    // There will be no POST data the first time this function is
                    // called.
                    con = new connection_status;
                    *con_cls = (void*)con;

                    con->post_processor = MHD_create_post_processor(
                            connection,
                            65536,
                            postdata_iterator,
                            *con_cls
                            );
                    return MHD_YES;
                }

                con = (connection_status*)(*con_cls);

                if(*upload_data_size == 0)
                {
                    // Upload has finished.
                    std::cerr << "data size " << con->data_size << std::endl;
                    // Try to insert the photograph.
                    std::string datetime;
                    try
                    {
                        auto image = Exiv2::ImageFactory::open(
                                (con->jpeg_data.data()),
                                static_cast<long>(con->data_size)
                                );
                        image->readMetadata();
                        Exiv2::ExifKey key("Exif.Photo.DateTimeOriginal");

                        // Try to find the date key
                        Exiv2::ExifData::iterator pos = image->exifData().findKey(
                                Exiv2::ExifKey("Exif.Image.DateTimeOriginal")
                                );
                        if(pos == image->exifData().end())
                            pos = image->exifData().findKey(
                                    Exiv2::ExifKey("Exif.Image.DateTime")
                                    );
                        // If an acceptable key was found
                        if(pos != image->exifData().end())
                        {
                            datetime = pos->getValue()->toString();
                            std::cerr << "datetime " << datetime << std::endl;
                        }

                        if(datetime.length() < 19)
                            throw std::runtime_error(
                                    (slide::mkstr() <<
                                         "datetime is of wrong length (" <<
                                         datetime.length() <<
                                         ", should be 19)").str().c_str()
                                    );

                        datetime[4] = '-';
                        datetime[7] = '-';
                        datetime[10] = 'T';
                    }
                    catch(const std::exception& e)
                    {
                        std::cerr << "warning: failed to get a date time from the image" << std::endl;
                        std::cerr << e.what() << std::endl;
                    }

                    int photograph_id = 0;

                    try
                    {
                        slide::transaction tr(database(), "insertphotograph");
                        auto photograph = slide::row<std::string, std::string, std::string>::make_row(
                                con->title,
                                con->caption,
                                datetime
                                );
                        slide::devoid(
                                "INSERT INTO helios_photograph(title, caption, taken) "
                                "VALUES(?, ?, ?) ",
                                photograph,
                                database()
                                );
                        photograph_id= slide::last_insert_rowid(database());
                        std::cerr << "photograph id " << photograph_id << std::endl;
                        auto photograph_location = slide::row<int, std::string>::make_row(
                                photograph_id,
                                con->location
                                );
                        std::cerr << "photograph_location " << con->location << std::endl;
                        slide::devoid(
                                "INSERT INTO helios_photograph_location(photograph_id, location) "
                                "VALUES(?, ?) ",
                                photograph_location,
                                database()
                                );
                        sqlite3_stmt *stmt;
                        sqlite3_prepare(
                                database().handle(),
                                "INSERT INTO helios_jpeg_data(photograph_id, data) VALUES (?, ?)",
                                -1,
                                &stmt,
                                nullptr
                                );
                        sqlite3_bind_int(stmt, 1, photograph_id);
                        sqlite3_bind_blob(
                                stmt,
                                2,
                                con->jpeg_data.data(),
                                static_cast<int>(con->data_size),
                                SQLITE_TRANSIENT
                                );
                        slide::step(stmt);
                        sqlite3_finalize(stmt);
                        tr.commit();
                    }
                    catch(const std::exception& e)
                    {
                        std::cerr << "error: inserting photograph into database: " << e.what() << std::endl;
                        throw webserver::public_exception(
                                "failed to insert photograph into database"
                                );
                    }

                    MHD_destroy_post_processor(con->post_processor);
                    delete con;

                    char response_data = 0;
                    struct MHD_Response *response = MHD_create_response_from_buffer(
                            0,
                            &response_data,
                            MHD_RESPMEM_MUST_COPY
                            );
                    MHD_add_response_header(
                            response,
                            "Location",
                            (slide::mkstr() << "/photograph.html#" << photograph_id).str().c_str()
                            );
                    int ret = MHD_queue_response(connection, 303, response);
                    MHD_destroy_response(response);
                    return ret;
                }

                if(MHD_post_process(con->post_processor, upload_data, *upload_data_size) != MHD_YES)
                    std::cerr << "post_process error" << std::endl;
                *upload_data_size = 0;
                return MHD_YES;
            }
        private:
    };

    int postdata_iterator(
            void *cls,
            enum MHD_ValueKind kind,
            const char *key,
            const char */*filename*/,
            const char */*content_type*/,
            const char */*transfer_encoding*/,
            const char *data,
            uint64_t off,
            size_t size
            )
    {
        upload_function::connection_status *con = (upload_function::connection_status*)cls;

        if(std::string(key) == "title" && kind == MHD_POSTDATA_KIND)
            con->title = std::string(data, size);

        if(std::string(key) == "caption" && kind == MHD_POSTDATA_KIND)
            con->caption = std::string(data, size);

        if(std::string(key) == "location" && kind == MHD_POSTDATA_KIND)
            con->location = std::string(data, size);

        if(std::string(key) == "jpeg" && kind == MHD_POSTDATA_KIND)
        {
            con->data_size = std::max(con->data_size, static_cast<std::size_t>(off + size));
            con->jpeg_data.resize(con->data_size);
            std::memcpy((con->jpeg_data.data() + off), data, size);
        }

        return MHD_YES;
    }
}

namespace rd_server
{
    namespace attr
    {
        constexpr const char title[] = "title";
        //constexpr const char caption[] = "caption";
        constexpr const char taken[] = "taken";
        constexpr const char location[] = "location";
        constexpr const char name[] = "name";
        constexpr const char tag[] = "tag";
        constexpr const char id[] = "id";

        // Use photograph_id to differentiate from other ids in the same
        // object.
        constexpr const char photograph_id[] = "photograph_id";
    }
}

int main(const int argc, char * const argv[])
{
    using namespace rd_server;

    uint16_t port = 4000;

    int option;
    while((option = getopt(argc, argv, "p:d:")) != -1)
    {
        switch(option)
        {
            case 'p':
                if(optarg)
                    try
                    {
                        port = static_cast<uint16_t>(
                                std::stoi(optarg)
                                );
                    }
                    catch(const std::exception&)
                    {
                    }
                break;
            case 'd':
                if(optarg)
                    set_db_path(optarg);
                break;
        }
    }

    create_db();

    std::cerr << "Starting server on port " << port << "..." << std::endl;

    auto install_static_request_function = [](
            const std::string& url,
            const std::string& content,
            const std::string mimetype = "text/html"
            )
    {
        webserver::install_request_function(
            std::unique_ptr<webserver::request_function>(
                new webserver::static_request_function(
                    url,
                    content,
                    mimetype
                    )
                )
            );
    };

    webserver::install_not_found_function(
            std::unique_ptr<webserver::request_function>(
                new webserver::static_request_function(
                    "",
                    WEB_STATIC_STD_STRING(web_404_html),
                    "text/html"
                    )
                )
            );

            //new text_request_function(
                //"/",
                //"GET",
                //[](const std::string param) {
                    //std::cerr << "param " << param << std::endl;
                    //return "<h1>Hello World</h1>";
                //}
                //)
    //webserver::install_request_function(
        //std::unique_ptr<webserver::request_function>(
            //new webserver::static_request_function(
                //"/",
                //WEB_STATIC_STD_STRING(web_index_html)
                //)
            //)
        //);

    install_static_request_function("/", WEB_STATIC_STD_STRING(web_index_html));
    install_static_request_function("/albums.html", WEB_STATIC_STD_STRING(web_albums_html));
    install_static_request_function("/application.js", WEB_STATIC_STD_STRING(web_application_js), "text/javascript");
    install_static_request_function("/backbone.js", WEB_STATIC_STD_STRING(web_backbone_js), "text/javascript");
    install_static_request_function("/grid.css", WEB_STATIC_STD_STRING(web_grid_css), "text/css");
    install_static_request_function("/index.html", WEB_STATIC_STD_STRING(web_index_html));
    install_static_request_function("/jquery.js", WEB_STATIC_STD_STRING(web_jquery_js), "text/javascript");
    install_static_request_function("/modal.css", WEB_STATIC_STD_STRING(web_modal_css), "text/css");
    install_static_request_function("/modal.js", WEB_STATIC_STD_STRING(web_modal_js), "text/javascript");
    install_static_request_function("/photograph.html", WEB_STATIC_STD_STRING(web_photograph_html));
    install_static_request_function("/style.css", WEB_STATIC_STD_STRING(web_style_css), "text/css");
    install_static_request_function("/teletype-theme.css", WEB_STATIC_STD_STRING(web_teletype_theme_css), "text/css");
    install_static_request_function("/underscore.js", WEB_STATIC_STD_STRING(web_underscore_js), "text/javascript");
    install_static_request_function("/views.js", WEB_STATIC_STD_STRING(web_views_js), "text/javascript");
    //install_static_request_function("/", WEB_STATIC_STD_STRING(web_));
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album",
                    "GET",
                    [](const std::string& param, const std::string&) -> std::string
                    {
                        if(param.length())
                        {
                            int id = 0;
                            try
                            {
                                id = std::stoi(param);
                            }
                            catch(const std::exception&)
                            {
                                throw webserver::public_exception("Not an integer");
                            }

                            try
                            {
                                return slide::get_row<int, std::string>(
                                        database(),
                                        "SELECT album_id, name FROM helios_album "
                                        " WHERE album_id = ? ",
                                        slide::row<int>::make_row(id)
                                        ).to_json<attr::id, attr::name>();
                            }
                            catch(const slide::exception& e)
                            {
                                throw webserver::public_exception("No album with that id");
                            }
                        }
                        else
                        {
                            return slide::get_collection<int, std::string>(
                                        database(),
                                        "SELECT album_id, name FROM helios_album "
                                        " ORDER BY name "
                                        ).to_json<attr::id, attr::name>();
                        }
                    }
                    )
                )
            );
    // Get the list of albums a photograph is in.
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph_album",
                    "GET",
                    [](const std::string& param, const std::string&) -> std::string
                    {
                        const int photograph_id = std::stoi(param);
                        return slide::get_collection<int, std::string>(
                                database(),
                                "SELECT album_id, name FROM helios_album "
                                "NATURAL JOIN helios_photograph_in_album "
                                "WHERE photograph_id = ? "
                                "ORDER BY name ",
                                slide::row<int>::make_row(photograph_id)
                                ).to_json<attr::id, attr::name>();
                    }
                    )
                )
            );
    // Set the list of albums a photograph is in.
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph_album",
                    "PUT",
                    [](const std::string& param, const std::string& data) -> std::string
                    {
                        const int photograph_id = std::stoi(param);
                        slide::transaction tr(database(), "albumphotograph");
                        slide::devoid(
                                "DELETE FROM helios_photograph_in_album "
                                "WHERE photograph_id = ? ",
                                slide::row<int>::make_row(photograph_id),
                                database()
                                );
                        auto c = slide::collection<int, int>
                            ::from_json<attr::photograph_id, attr::id>(data);
                        c.set_attr<0>(photograph_id);
                        slide::devoid(
                                "INSERT INTO helios_photograph_in_album(photograph_id, album_id) "
                                "VALUES(?, ?) ",
                                c,
                                database()
                                );
                        tr.commit();
                        return slide::get_collection<int, std::string, int>(
                                database(),
                                "SELECT album_id, name, photograph_id FROM helios_album "
                                "NATURAL JOIN helios_photograph_in_album "
                                "WHERE photograph_id = ? "
                                "ORDER BY name ",
                                slide::row<int>::make_row(photograph_id)
                                ).to_json<attr::id, attr::name, attr::photograph_id>();
                    }
                    )
                )
            );
    // Get the list of tags applied to a photograph.
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph_tag",
                    "GET",
                    [](const std::string& param, const std::string&) -> std::string
                    {
                        const int photograph_id = std::stoi(param);
                        return slide::get_collection<std::string>(
                                database(),
                                "SELECT tag FROM helios_photograph_tagged "
                                "WHERE photograph_id = ? "
                                "ORDER BY tag ",
                                slide::row<int>::make_row(photograph_id)
                                ).to_json<attr::tag>();
                    }
                    )
                )
            );
    // Set the list of tags applied to a photograph.
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph_tag",
                    "PUT",
                    [](const std::string& param, const std::string& data) -> std::string
                    {
                        std::cerr << "update tags " << data << std::endl;
                        const int photograph_id = std::stoi(param);
                        slide::transaction tr(database(), "photographtagged");
                        slide::devoid(
                                "DELETE FROM helios_photograph_tagged "
                                "WHERE photograph_id = ? ",
                                slide::row<int>::make_row(photograph_id),
                                database()
                                );
                        auto c = slide::collection<int, std::string>::from_json<attr::id, attr::tag>(data);
                        c.set_attr<0>(photograph_id);
                        for(slide::row<int, std::string> r : c)
                            std::cerr << "row " << r.get<0>() << " " << r.get<1>() << std::endl;
                        slide::devoid(
                                "INSERT INTO helios_photograph_tagged(photograph_id, tag) "
                                "VALUES(?, ?) ",
                                c,
                                database()
                                );
                        tr.commit();
                        return slide::get_collection<std::string>(
                                database(),
                                "SELECT tag FROM helios_photograph_tagged "
                                "WHERE photograph_id = ? "
                                "ORDER BY tag ",
                                slide::row<int>::make_row(photograph_id)
                                ).to_json<attr::tag>();
                    }
                    )
                )
            );
    // Get a list of photographs in an album.
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album_photograph",
                    "GET",
                    [](const std::string& param, const std::string&) -> std::string
                    {
                        const int album_id = std::stoi(param);
                        return slide::get_collection<int, std::string, std::string, std::string>(
                                database(),
                                "SELECT helios_photograph.photograph_id, title, location, taken "
                                "FROM helios_photograph "
                                "LEFT OUTER JOIN helios_photograph_location "
                                "ON helios_photograph.photograph_id = helios_photograph_location.photograph_id "
                                "JOIN helios_photograph_in_album "
                                "ON helios_photograph.photograph_id = helios_photograph_in_album.photograph_id "
                                "WHERE album_id = ?",
                                slide::row<int>::make_row(album_id)
                                ).to_json<attr::id, attr::title, attr::location, attr::taken>();
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album_photograph/uncategorised",
                    "GET",
                    [](const std::string&, const std::string&) -> std::string
                    {
                        return slide::get_collection<int, std::string, std::string, std::string>(
                                database(),
                                "SELECT helios_photograph.photograph_id, title, location, taken "
                                "FROM helios_photograph "
                                "LEFT OUTER JOIN helios_photograph_location "
                                "ON helios_photograph.photograph_id = helios_photograph_location.photograph_id "
                                "LEFT OUTER JOIN helios_photograph_in_album "
                                "ON helios_photograph.photograph_id = helios_photograph_in_album.photograph_id "
                                "WHERE album_id IS NULL"
                                ).to_json<attr::id, attr::title, attr::location, attr::taken>();
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album",
                    "PUT",
                    [](const std::string& param, const std::string& post)
                    {
                        if(std::stoi(param) != slide::row<int>::from_json<attr::id>(post).get<0>())
                            throw webserver::public_exception("Ids don't match");
                        if(
                                slide::devoid(
                                    "UPDATE helios_album SET album_id = ?, name = ? "
                                    " WHERE album_id = ? ",
                                    slide::row<std::string, int>
                                        ::from_json<attr::id, attr::name>(post)
                                        .cat(slide::row<int>::make_row(std::stoi(param))),
                                    database()
                                    ) > 0
                          )
                            return slide::get_row<int, std::string>(
                                    database(),
                                    "SELECT album_id, name FROM helios_album WHERE album_id = ? ",
                                    slide::row<int>::from_json<attr::id>(post)
                                    ).to_json<attr::id, attr::name>();
                        throw webserver::public_exception("Updating album");
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album",
                    "POST",
                    [](const std::string&, const std::string& data)
                    {
                        slide::devoid(
                            "INSERT INTO helios_album(name) values(?)",
                            slide::row<std::string>::from_json<attr::name>(data),
                            database()
                            );
                        return slide::get_row<int, std::string>(
                                database(),
                                "SELECT album_id, name FROM helios_album WHERE album_id = ? ",
                                slide::row<int>::make_row(slide::last_insert_rowid(database()))
                                ).to_json<attr::id, attr::name>();
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/album",
                    "DELETE",
                    [](const std::string& param, const std::string&)
                    {
                        if(
                                slide::devoid(
                                    "DELETE FROM helios_album WHERE album_id = ?",
                                    slide::row<int>::make_row(std::stoi(param)),
                                    database()
                                    ) > 0
                          )
                            return "deleted";
                        throw webserver::public_exception("deleting album");
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph",
                    "GET",
                    [](const std::string& param, const std::string&)
                    {
                        if(param.length())
                            return slide::get_row<int, std::string, std::string, std::string>(
                                    database(),
                                    "SELECT helios_photograph.photograph_id, title, location, taken "
                                    "FROM helios_photograph "
                                    "LEFT OUTER JOIN helios_photograph_location "
                                    "ON helios_photograph.photograph_id = helios_photograph_location.photograph_id "
                                    "WHERE helios_photograph.photograph_id = ?",
                                    slide::row<int>::make_row(std::stoi(param))
                                    ).to_json<attr::id, attr::title, attr::location, attr::taken>();
                        else
                            throw webserver::public_exception("can't get all photographs");
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph",
                    "PUT",
                    [](const std::string& /*param*/, const std::string& data)
                    {
                        // TODO param should match id in JSON
                        slide::devoid(
                                "UPDATE helios_photograph SET title = ?, "
                                "taken = ? "
                                "WHERE photograph_id = ?",
                                slide::row<std::string, std::string, int>
                                    ::from_json<attr::title, attr::taken, attr::id>(data),
                                database()
                                );
                        slide::devoid(
                                "UPDATE helios_photograph_location SET location = ? "
                                "WHERE photograph_id = ?",
                                slide::row<std::string, int>
                                    ::from_json<attr::location, attr::id>(data),
                                database()
                                );
                        return slide::get_row<int, std::string, std::string, std::string>(
                                database(),
                                "SELECT helios_photograph.photograph_id, title, taken, location FROM helios_photograph "
                                "LEFT OUTER JOIN helios_photograph_location "
                                "ON helios_photograph.photograph_id = helios_photograph_location.photograph_id "
                                "WHERE helios_photograph.photograph_id = ? ",
                                slide::row<int>::from_json<attr::id>(data)
                                ).to_json<attr::id, attr::title, attr::taken, attr::location>();
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/photograph/small",
                    "GET",
                    [](const std::string& param, const std::string&)
                    {
                        std::vector<unsigned char> jpeg = get_small_jpeg(std::stoi(param));
                        return std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                    },
                    "image/jpeg"
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/photograph/medium",
                    "GET",
                    [](const std::string& param, const std::string&)
                    {
                        std::vector<unsigned char> jpeg = get_medium_jpeg(std::stoi(param));
                        return std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                    },
                    "image/jpeg"
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/photograph/original",
                    "GET",
                    [](const std::string& param, const std::string&)
                    {
                        std::vector<unsigned char> jpeg = get_fullsize_jpeg(std::stoi(param));
                        return std::string(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
                    },
                    "image/jpeg"
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(
                new webserver::text_request_function(
                    "/api/photograph",
                    "DELETE",
                    [](const std::string& param, const std::string&)
                    {
                        if(
                                slide::devoid(
                                    "DELETE FROM helios_photograph WHERE photograph_id = ?",
                                    slide::row<int>::make_row(std::stoi(param)),
                                    database()
                                    )
                                < 1
                          )
                            throw webserver::public_exception("Deleting photograph");

                        return "ok";
                    }
                    )
                )
            );
    webserver::install_request_function(
            webserver::request_function_ptr(new upload_function)
            );

    webserver::start_server(port);

    std::cerr << "Server started, press return to exit." << std::endl;
    std::cin.get();
    std::cerr << "Shutting down..." << std::endl;

    webserver::stop_server();

    return 0;
}

