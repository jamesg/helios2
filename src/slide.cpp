#include "slide.hpp"

#include <algorithm>

std::string slide::escape(const std::string& str)
{
    std::string out;
    // Avoid having to reallocate the string.
    out.reserve((std::size_t)((double)str.length() * 1.1 + 4.0));
    std::for_each(
            str.cbegin(),
            str.cend(),
            [&out](const char c) {
                switch(c)
                {
                // Escape the quote and backslash characters.
                case '"':
                case '\\':
                    out += '\\';
                    // Fall through.
                default:
                    out += c;
                }
            }
            );
    return out;
}
std::string slide::unescape(const std::string& str)
{
    std::string out;
    out.reserve(str.length());
    // The next character should be copied as a literal.
    bool ign = false;
    std::for_each(
            str.cbegin(),
            str.cend(),
            [&out, &ign](const char c) {
                if(!ign && c == '\\')
                    ign = true;
                else
                {
                    ign = false;
                    out += c;
                }
            }
            );
    return out;
}
void slide::detail::set_value(bool value, bool& out)
{
    out = value;
}
void slide::detail::set_value(double value, bool& out)
{
    out = (value != 0.0);
}
void slide::detail::set_value(int value, bool& out)
{
    out = (value != 0);
}
void slide::detail::set_value(std::string value, bool& out)
{
    out = (value.length() > 0);
}
void slide::detail::set_value(bool value, int& out)
{
    out = value ? 1 : 0;
}
void slide::detail::set_value(double value, int& out)
{
    out = static_cast<int>(value);
}
void slide::detail::set_value(int value, int& out)
{
    out = value;
}
void slide::detail::set_value(std::string value, int& out)
{
    try
    {
        out = std::stoi(value);
    }
    catch(const std::invalid_argument&)
    {
        out = 0;
    }
}
void slide::detail::set_value(bool value, double& out)
{
    out = value ? 1.0 : 0.0;
}
void slide::detail::set_value(double value, double& out)
{
    out = value;
}
void slide::detail::set_value(int value, double& out)
{
    out = static_cast<double>(value);
}
void slide::detail::set_value(std::string value, double& out)
{
    try
    {
        out = std::stod(value);
    }
    catch(const std::invalid_argument&)
    {
        out = 0;
    }
}
void slide::detail::set_value(bool value, std::string& out)
{
    out = value ? "true" : "false";
}
void slide::detail::set_value(double value, std::string& out)
{
    out = (mkstr() << value);
}
void slide::detail::set_value(int value, std::string& out)
{
    out = (mkstr() << value);
}
void slide::detail::set_value(std::string value, std::string& out)
{
    out = value;
}
void slide::detail::json_str(bool b, std::ostringstream& oss)
{
    oss << (b ? "true" : "false");
}
void slide::detail::json_str(double d, std::ostringstream& oss)
{
    oss << d;
}
void slide::detail::json_str(int i, std::ostringstream& oss)
{
    oss << i;
}
void slide::detail::json_str(std::string str, std::ostringstream& oss)
{
    oss << "\"" << escape(str) << "\"";
}
int slide::devoid(const std::string& query, connection& db)
{
    return devoid(query, row<>(), db);
}
void slide::detail::get_column(sqlite3_stmt *stmt, std::size_t index, bool& value)
{
    value = (sqlite3_column_int(stmt, static_cast<int>(index)) != 0);
}
void slide::detail::get_column(sqlite3_stmt *stmt, std::size_t index, double& value)
{
    value = sqlite3_column_double(stmt, static_cast<int>(index));
}
void slide::detail::get_column(sqlite3_stmt *stmt, std::size_t index, int& value)
{
    value = sqlite3_column_int(stmt, static_cast<int>(index));
}
void slide::detail::get_column(sqlite3_stmt *stmt, std::size_t index, std::string& value)
{
    sqlite3_column_blob(stmt, static_cast<int>(index));
    std::size_t n_bytes = static_cast<std::size_t>(
            sqlite3_column_bytes(stmt, static_cast<int>(index))
            );
    const void *p = sqlite3_column_blob(stmt, static_cast<int>(index));

    value.resize(n_bytes, 0);

    ::memcpy(reinterpret_cast<void*>(&(value[0])), p, n_bytes);
}
void slide::detail::bind_value(bool value, std::size_t index, sqlite3_stmt *stmt)
{
    sqlite3_bind_int(stmt, static_cast<int>(index), value ? 1 : 0);
}
void slide::detail::bind_value(double value, std::size_t index, sqlite3_stmt *stmt)
{
    sqlite3_bind_double(stmt, static_cast<int>(index), value);
}
void slide::detail::bind_value(int value, std::size_t index, sqlite3_stmt *stmt)
{
    sqlite3_bind_int(stmt, static_cast<int>(index), value);
}
void slide::detail::bind_value(std::string value, std::size_t index, sqlite3_stmt *stmt)
{
#ifdef SLIDE_ENABLE_DEBUGGING
    std::cerr << "binding string \"" <<
        ((value.length() > 100) ? "<< TRUNCATED >>" : value) <<
        "\" to index " << index << std::endl;
#endif
    if(
            sqlite3_bind_blob(
                stmt,
                static_cast<int>(index),
                value.c_str(),
                static_cast<int>(value.length()),
                SQLITE_TRANSIENT
                ) != SQLITE_OK
            )
        throw exception(
            mkstr() << "binding string \"" <<
                ((value.length() > 100) ? "<< TRUNCATED >>" : value) <<
                "\" to index " << index
            );
}

