#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "imageutils_nowarnings.hpp"
#include "slide.hpp"

int main(const int argc, char * const argv[])
{
    std::string album_name, db_file, output_dir;
    bool fullsize = false, scaled = false;
    int option;
    while((option = getopt(argc, argv, "a:d:fo:s")) != -1)
    {
        switch(option)
        {
            case 'a':
                if(optarg)
                    album_name = optarg;
                break;
            case 'd':
                if(optarg)
                    db_file = optarg;
                break;
            case 'f':
                fullsize = true;
                break;
            case 'o':
                if(optarg)
                    output_dir = optarg;
                break;
            case 's':
                scaled = true;
                break;
        }
    }

    if(db_file.empty())
        throw std::runtime_error("db file not provided");

    if(!(fullsize ^ scaled))
        throw std::runtime_error(
                "one of -f (fullsize) and -s (scaled) must be provided"
                );

    slide::connection database(db_file);

    auto get_fullsize_jpeg =
        [&database](const int photograph_id) -> std::vector<unsigned char>
    {
        std::cerr << "get_fullsize_jpeg " << photograph_id << std::endl;
        sqlite3_stmt *stmt;
        sqlite3_prepare(
                database.handle(),
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
    };

    auto export_photograph = [&database, fullsize, &get_fullsize_jpeg](
                const int photograph_id,
                const std::string& filename
                )
    {
        const std::string width = "960";
        const std::string height = "640";

        std::cerr << "Exporting photograph " << photograph_id << std::endl;

        if(FILE *file = fopen(filename.c_str(), "r"))
        {
            fclose(file);
            return;
        }

        std::vector<unsigned char> fullsize_jpeg = get_fullsize_jpeg(photograph_id);

        std::ofstream os(filename);
        if(fullsize)
        {
            os << std::string(
                    (const char*)(&(fullsize_jpeg.data()[0])),
                    fullsize_jpeg.size()
                    );
        }
        else
        {
            Magick::Blob out;
            Magick::Image image(Magick::Blob(
                reinterpret_cast<const void*>(&(fullsize_jpeg[0])), fullsize_jpeg.size())
                );
            long orientation = 1;
            try
            {   // Retrieve orientation
                auto exiv_image = Exiv2::ImageFactory::open(
                    reinterpret_cast<const unsigned char*>(&(fullsize_jpeg[0])),
                    static_cast<long>(fullsize_jpeg.size())
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
            os << std::string((const char*)out.data(), out.length());
        }
    };

    auto export_album = [&database, &export_photograph, output_dir](
            const slide::row<int, std::string> album
            )
    {
        std::cerr << "Exporting album " << album.get<1>() << std::endl;
        std::string directory = slide::mkstr() << output_dir << '/' << album.get<1>();
        if(mkdir(directory.c_str(), 0755) != 0 && errno != EEXIST)
            throw std::runtime_error(
                    slide::mkstr() << "creating directory " << directory
                    );

        slide::collection<int, std::string, std::string> photographs =
            slide::get_collection<int, std::string, std::string>(
                database,
                "SELECT photograph_id, taken, title "
                "FROM helios_photograph "
                "NATURAL JOIN helios_photograph_in_album "
                "WHERE album_id = ? "
                "ORDER BY taken",
                slide::row<int>::make_row(album.get<0>())
                );

        std::string last_taken;
        int index = 0;
        for(const slide::row<int, std::string, std::string> photograph : photographs)
        {
            std::cerr << "Photograph " << photograph.get<2>() << " taken " <<
                photograph.get<1>() << std::endl;

            const std::string taken = photograph.get<1>().length() ?
                std::string(photograph.get<1>(), 0, 10) : "unknown";

            if(taken == last_taken)
                ++index;
            else
                index = 1;

            last_taken = taken;

            const std::string filename = slide::mkstr() <<
                directory << '/' << taken << std::setfill('0') << '_' <<
                std::setw(4) << index << ".jpg";

            export_photograph(photograph.get<0>(), filename);
        }
    };

    if(album_name.empty())
    {
        // Export all the albums.
        slide::collection<int, std::string> albums =
            slide::get_collection<int, std::string>(
                database,
                "SELECT album_id, name FROM helios_album"
                );
        for(const slide::row<int, std::string> album : albums)
            export_album(album);
    }
    else
    {
        // Export just one album.
        slide::row<int, std::string> album = slide::get_row<int, std::string>(
                database,
                "SELECT album_id, name FROM helios_album "
                "WHERE name LIKE ? ",
                slide::row<std::string>::make_row(album_name)
                );
        export_album(album);
    }
}

