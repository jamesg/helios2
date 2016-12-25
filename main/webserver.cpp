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
    std::string g_db_path = "rd.db";

    slide::connection& database()
    {
        static std::unique_ptr<slide::connection> g_conn;
        if(!g_conn)
            try
            {
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
        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("retrieving fullsize");
        }
        std::vector<unsigned char> out(
                (unsigned char*)sqlite3_column_blob(stmt, 0),
                (unsigned char*)sqlite3_column_blob(stmt, 0) + sqlite3_column_bytes(stmt, 0)
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
        sqlite3_step(stmt);
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
        if(sqlite3_step(stmt) != SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            throw std::runtime_error("retrieving jpeg data");
        }
        std::vector<unsigned char> out(
                (unsigned char*)sqlite3_column_blob(stmt, 0),
                (unsigned char*)sqlite3_column_blob(stmt, 0) + sqlite3_column_bytes(stmt, 0)
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
}

namespace rd_server
{
    namespace attr
    {
        constexpr const char title[] = "title";
        //constexpr const char caption[] = "caption";
        constexpr const char taken[] = "taken";
        constexpr const char location[] = "location";
        //constexpr const char data[] = "data";
        constexpr const char name[] = "name";
        //constexpr const char tag[] = "tag";

        constexpr const char id[] = "id";
    }
}

int main(const int argc, char * const argv[])
{
    using namespace rd_server;

    uint16_t port = 8088;

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
                    g_db_path = optarg;
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
                            "INSERT INTO helios_album(album_id, name) values(?, ?)",
                            slide::row<int, std::string>::from_json<attr::id, attr::name>(data),
                            database()
                            );
                        return slide::get_row<int, std::string>(
                                database(),
                                "SELECT album_id, name FROM helios_album WHERE album_id = ? ",
                                slide::row<int>::from_json<attr::id>(data)
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
    //webserver::install_request_function(
            //webserver::request_function_ptr(
                //new webserver::text_request_function(
                    //"/photograph/medium",
                    //"GET",
                    //[](const std::string& param, const std::string&)
                    //{
                    //},
                    //"image/jpeg"
                    //)
                //)
            //);
    //webserver::install_request_function(
            //webserver::request_function_ptr(
                //new webserver::text_request_function(
                    //"/api/photograph",
                    //"POST",
                    //[](const std::string&, const std::string& data)
                    //{
                        //try
                        //{
                            //auto r = slide::row<std::string>
                                //::from_json<attr::title>(data);
                            //slide::devoid(
                                    //"INSERT INTO helios_photograph(title, taken) "
                                    //"VALUES(?, ?) ",
                                    //r,
                                    //database()
                                    //);
                        //}
                        //catch(const std::exception&)
                        //{
                            //throw;
                        //}
                    //}
                    //)
                //)
            //);
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

    webserver::start_server(port);

    std::cerr << "Server started, press return to exit." << std::endl;
    getchar();

    webserver::stop_server();

    return 0;
}

