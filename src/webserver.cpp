#include "webserver.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <vector>

namespace
{
    char s_error_str[] = "Unknown error";

    std::vector<webserver::request_function_ptr> g_request_functions;
    webserver::request_function_ptr g_not_found_function;

    int answer_connection(
            void *cls,
            struct MHD_Connection *connection,
            const char *url,
            const char *method,
            const char *version,
            const char *upload_data,
            size_t *upload_data_size,
            void **con_cls
            )
    {
        webserver::request_function *fn = nullptr;
        int best_match = 0;
        for(const std::unique_ptr<webserver::request_function>& url_fn : g_request_functions)
        {
            const int strength = url_fn->match_strength(url, method);
            if(strength > 0 && strength > best_match)
            {
                fn = url_fn.get();
                best_match = strength;
            }
        }

        if(fn != nullptr)
            try
            {
                return (*fn)(cls, connection, url, method, version, upload_data, upload_data_size, con_cls);
            }
            catch(const std::exception& e)
            {
                std::cerr << "Error in request function: " << e.what() << std::endl;
            }

        if(g_not_found_function)
            return (*g_not_found_function)(cls, connection, url, method, version, upload_data, upload_data_size, con_cls);

        return -1;
    }
}

/*
 * Get the length of the substring in strings a and b that match, starting
 * at the beginning of each string.
 */
std::size_t webserver::matching_length(const std::string& a, const std::string& b)
{
    for(std::size_t i = 0; i < std::min(a.length(), b.length()); ++i)
        if(a[i] != b[i])
            return i;
    return std::min(a.length(), b.length());
}

webserver::text_request_function::text_request_function(
        const std::string& url,
        const std::string& method,
        function_type fn,
        std::string mimetype
        ) :
    m_url(url),
    m_method(method),
    m_mimetype(mimetype),
    m_function(fn)
{
}
int webserver::text_request_function::match_strength(
        const char *url,
        const char *method
        )
{
    if(m_method != method)
        return 0;
    const std::size_t ml = matching_length(m_url, url);
    if(ml >= m_url.length())
        return static_cast<int>(ml);
    return 0;
}
int webserver::text_request_function::operator()(
        void *cls,
        struct MHD_Connection *connection,
        const char *url,
        const char *method,
        const char */*version*/,
        const char *upload_data,
        size_t *upload_data_size,
        void **con_cls
        )
{
    struct connection_status
    {
        std::ostringstream oss;
    };

    connection_status *con = nullptr;

    const std::string param = m_url.length() < std::string(url).length() ?
        std::string(url).substr(m_url.length() + 1) : "";

    if(cls == nullptr)
        std::cerr << "request " << method << " " << url << "  (param " <<
            param << ")" << std::endl;

    if(std::string(method) == "POST" || std::string(method) == "PUT")
    {
        if(*con_cls == nullptr)
        {
            // There will be no POST data the first time this function is
            // called.
            con = new connection_status;
            *con_cls = (void*)con;
            return MHD_YES;
        }

        con = (connection_status*)(*con_cls);

        if(upload_data != nullptr && *upload_data_size != 0)
        {
            // Data is being received from the client.
            std::cerr << "Upload data " << upload_data << std::endl;
            con->oss << std::string(upload_data, *upload_data_size);
            *upload_data_size = 0;
            return MHD_YES;
        }

        // upload_data_size equal to 0 indicates that all data has been
        // received.
    }

    try
    {
        const std::string post = (con != nullptr) ? con->oss.str() : "";
        if(con != nullptr)
        {
            delete con;
            con = nullptr;
            *con_cls = nullptr;
        }
        const std::string str = m_function(param, post);
        struct MHD_Response *response = MHD_create_response_from_buffer(
                str.length(),
                const_cast<char*>(str.c_str()),
                MHD_RESPMEM_MUST_COPY
                );
        MHD_add_response_header(response, "Content-Type", m_mimetype.c_str());
        int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
        MHD_destroy_response(response);
        return ret;
    }
    catch(const public_exception& e)
    {
        struct MHD_Response *response = MHD_create_response_from_buffer(
                strlen(e.what()),
                const_cast<char*>(e.what()),
                MHD_RESPMEM_MUST_COPY
                );
        int ret = MHD_queue_response(connection, 500, response);
        MHD_destroy_response(response);
        std::cerr << "error in text request function (relayed to client): " << e.what() << std::endl;
        return ret;
    }
    catch(const std::exception& e)
    {
        struct MHD_Response *response = MHD_create_response_from_buffer(
                strlen(s_error_str),
                s_error_str,
                MHD_RESPMEM_MUST_COPY
                );
        int ret = MHD_queue_response(connection, 500, response);
        MHD_destroy_response(response);
        std::cerr << "error in text request function: " << e.what() << std::endl;
        return ret;
    }
}

webserver::static_request_function::static_request_function(
        const std::string& url,
        const std::string& content,
        const std::string mimetype
        ) :
    m_url(url)
{
    m_response = MHD_create_response_from_buffer(
            content.length(),
            const_cast<char*>(content.c_str()),
            MHD_RESPMEM_MUST_COPY
            );
    if(m_response == nullptr)
        throw std::runtime_error("response object is null");
    MHD_add_response_header(m_response, "Content-Type", mimetype.c_str());
}
webserver::static_request_function::~static_request_function()
{
    MHD_destroy_response(m_response);
}
int webserver::static_request_function::match_strength(
        const char *url,
        const char *method
        )
{
    return (m_url == url && std::string(method) == "GET") ?
        static_cast<int>(m_url.length()) : 0;
}
int webserver::static_request_function::operator()(
        void */*cls*/,
        struct MHD_Connection *connection,
        const char *url,
        const char */*method*/,
        const char */*version*/,
        const char */*upload_data*/,
        size_t */*upload_data_size*/,
        void **/*con_cls*/
        )
{
    std::cerr << "request (static) " << url << std::endl;
    // Respond to the request immediately - there should be no POST data.
    return MHD_queue_response(connection, MHD_HTTP_OK, m_response);
}

void webserver::install_request_function(std::unique_ptr<request_function>&& fn)
{
    g_request_functions.push_back(std::move(fn));
}

namespace
{
    static struct MHD_Daemon *g_daemon = nullptr;
}

void webserver::start_server(uint16_t port)
{
    if(g_daemon == nullptr)
        g_daemon = MHD_start_daemon(
                MHD_USE_SELECT_INTERNALLY,
                port,
                nullptr,
                nullptr,
                &answer_connection,
                nullptr,
                MHD_OPTION_END
                );

    if(g_daemon == nullptr)
        throw std::runtime_error("starting MHD server");
}

void webserver::stop_server()
{
    if(g_daemon == nullptr)
        return;

    MHD_stop_daemon(g_daemon);
    g_daemon = nullptr;
}

void webserver::install_not_found_function(request_function_ptr&& fn)
{
    g_not_found_function = std::move(fn);
}

