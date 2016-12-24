#define CATCH_CONFIG_MAIN
#include "catch_nowarnings.hpp"

#include "slide.hpp"

namespace
{
    constexpr char t1_id[] = "t1_id";
    constexpr char t2_id[] = "t2_id";
}

SCENARIO("slide") {
    GIVEN("a string") {
        const std::string str = "\"abc\"";

        WHEN("the string is escaped") {
            const std::string str_esc = slide::escape(str);

            THEN("the string was escaped correctly") {
                REQUIRE(str_esc == "\\\"abc\\\"");
            }

            WHEN("the string is unescaped") {
                const std::string str_unesc = slide::unescape(str_esc);
                THEN("the string was unescaped correctly") {
                    REQUIRE(str_unesc == str);
                }
            }
        }
    }
    GIVEN("a simple json object string") {
        auto r = slide::row<std::string>::from_json<t1_id>(
                "{ \"t1_id\": \"1\" }"
                );

        THEN("the string was parsed correctly") {
            REQUIRE(r.get<0>() == "1");
        }
    }
    GIVEN("a json object string with a boolean") {
        auto r = slide::row<bool>::from_json<t1_id>(
                "{ \"t1_id\": true }"
                );

        THEN("the string was parsed correctly") {
            REQUIRE(r.get<0>() == true);
        }
    }
    GIVEN("a complex json object string") {
        const std::string json_str("{ \"t1_id\": \"1\", \"t2_id\": 2, \"aaa\": 123 }");

        WHEN("the number is parsed as a string") {
            auto r = slide::row<std::string>::from_json<t2_id>(json_str);
            THEN("the number was converted to a string") {
                REQUIRE(r.get<0>() == "2");
            }
        }

        WHEN("the string is parsed as a number") {
            auto r = slide::row<double>::from_json<t1_id>(json_str);
            THEN("the string was converted to a number") {
                REQUIRE(r.get<0>() == 1);
            }
        }
    }
    GIVEN("a json array") {
        const std::string json_str("[ { \"t1_id\": 1 } ]");

        WHEN("the string is parsed to a collection") {
            slide::collection<double> c = slide::collection<double>::from_json<t1_id>(json_str);
            THEN("the collection has one item") {
                REQUIRE(c.size() == 1);
            }
            THEN("the first item is correct") {
                REQUIRE(c.at(0).get<0>() == 1);
            }
        }
    }
    GIVEN("a Slide row") {
        const slide::row<std::string, double> r = slide::row<std::string, double>::make_row("1", 2);

        WHEN("the row is converted to a JSON string") {
            const std::string str = r.to_json<t1_id, t2_id>();
            THEN("the conversion was exactly correct") {
                REQUIRE(str == "{ \"t1_id\": \"1\", \"t2_id\": 2 }");
            }
        }
    }
    GIVEN("a Slide collection") {
        slide::collection<std::string, double> col;
        col.push_back(slide::row<std::string, double>::make_row("1", 2));
        col.push_back(slide::row<std::string, double>::make_row("3", 4));

        WHEN("the collection is converted to a JSON string") {
            const std::string str = col.to_json<t1_id, t2_id>();
            THEN("the conversion was exactly correct") {
                REQUIRE(str == "[ { \"t1_id\": \"1\", \"t2_id\": 2 }, { \"t1_id\": \"3\", \"t2_id\": 4 } ]");
            }
        }
    }
    GIVEN("a database") {
        slide::connection conn = slide::connection::in_memory_database();

        slide::devoid(
                "CREATE TABLE test (t1_id VARCHAR, t2_id VARCHAR);",
                conn
                );
        WHEN("data is inserted and read out as a JSON string") {
            slide::collection<std::string, double> in{
                slide::row<std::string, double>::make_row("1", 2),
                slide::row<std::string, double>::make_row("3", 4)
            };
            slide::devoid(
                    "INSERT INTO test(t1_id, t2_id) VALUES (?, ?)",
                    in,
                    conn
                    );
            slide::collection<std::string, double> col = slide::get_collection<std::string, double>(
                    conn,
                    "SELECT t1_id, t2_id FROM test ORDER BY t1_id;"
                    );
            const std::string str = col.to_json<t1_id, t2_id>();
            THEN("the conversion was exactly correct") {
                REQUIRE(str == "[ { \"t1_id\": \"1\", \"t2_id\": 2 }, { \"t1_id\": \"3\", \"t2_id\": 4 } ]");
            }
        }
    }
    GIVEN("a database") {
        slide::connection conn = slide::connection::in_memory_database();

        slide::devoid(
                "CREATE TABLE test (t1_id VARCHAR);",
                conn
                );
        slide::devoid(
                "INSERT INTO test(t1_id) VALUES(?);",
                slide::row<std::string>::make_row("str"),
                conn
                );

        WHEN("the data is retrieved") {
            slide::collection<std::string> col =
                slide::get_collection<std::string>(
                    conn,
                    "SELECT t1_id FROM test;"
                    );

            THEN("the collection has the correct size") {
                REQUIRE(col.size() == 1);
            }

            THEN("the first tuple is correct") {
                REQUIRE(col.at(0).get<0>() == "str");
            }
        }
    }
}

