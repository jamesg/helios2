#ifndef WEBSERVER_HPP
#define WEBSERVER_HPP

#include <memory>
#include <microhttpd.h>
#include <stdexcept>

namespace webserver
{
    std::size_t matching_length(const std::string& a, const std::string& b);

    /*
     * An exception with information that can be displayed to the client.
     */
    class public_exception :
        public std::runtime_error
    {
        public:
            public_exception(const std::string& message) :
                std::runtime_error(message)
            {
            }
    };

    class request_function
    {
        public:
            /*
             * Judge the strength of a match allowing this request function to
             * handle a request for a given URL and method.  Higher numbers
             * indicate a better match.  Any number less than 1 indicates no
             * match.
             */
            virtual int match_strength(const char *url, const char *method) = 0;
            /*
             * Handle an incoming request.  Receive the same arguments as the
             * MicroHTTPD handler function.
             */
            virtual int operator()(
                    void *cls,
                    struct MHD_Connection *connection,
                    const char *url,
                    const char *method,
                    const char *version,
                    const char *upload_data,
                    size_t *upload_data_size,
                    void **con_cls
                    ) = 0;
    };

    typedef std::unique_ptr<request_function> request_function_ptr;

    /*
     * Respond to a request with a simple text response.
     *
     * A request can have a URL parameter and in the case of PATCH/POST/PUT
     * requests, some data.  The URL parameter is obtained by stripping the
     * matching part of the URL plus a forward slash from the complete URL; for
     * instance, '/type/a/1' matched against '/type' gives the parameter 'a/1'.
     *
     * The response type is typically application/json, as this request
     * function is intended primarily to create APIs.  The response type can be
     * overridden by passing a fourth argument to the constructor.
     */
    class text_request_function : public request_function
    {
        public:
            typedef std::function<std::string(const std::string&, const std::string&)> function_type;
            text_request_function(
                    const std::string& url,
                    const std::string& method,
                    function_type fn,
                    std::string mimetype = "application/json"
                    );
            /*
             * Allow a match when the base URL is contained within the incoming
             * URL and the method matches exactly.
             */
            int match_strength(const char *url, const char *method) override;
            int operator()(
                    void *cls,
                    struct MHD_Connection *connection,
                    const char *url,
                    const char *method,
                    const char *version,
                    const char *upload_data,
                    size_t *upload_data_size,
                    void **con_cls
                    ) override;
        private:
            const std::string m_url;
            const std::string m_method;
            const std::string m_mimetype;
            const function_type m_function;
    };
    /*
     * A request function serving static data.
     */
    class static_request_function : public request_function
    {
        public:
            /*
             * Prepare a static response to send to clients.
             * The url, content string and mimetype are all copied into the
             * object.
             */
            static_request_function(
                    const std::string& url,
                    const std::string& content,
                    const std::string mimetype = "text/html"
                    );
            ~static_request_function();
            /*
             * An exact URL match is required.  The method must be "GET".
             */
            int match_strength(const char *url, const char *method) override;
            int operator()(
                    void */*cls*/,
                    struct MHD_Connection *connection,
                    const char */*url*/,
                    const char */*method*/,
                    const char */*version*/,
                    const char */*upload_data*/,
                    size_t */*upload_data_size*/,
                    void **/*con_cls*/
                    ) override;
        private:
            const std::string m_url;
            MHD_Response *m_response;
    };

    /*
     * Install a request function that will be used to serve requests for which
     * its match_strength function is highest.
     */
    void install_request_function(std::unique_ptr<request_function>&&);
    /*
     * Start the web server.  Has no effect if the server is already running.
     */
    void start_server(uint16_t port);
    /*
     * Stop the web server.  Has no effect if the sevrer is not running.
     */
    void stop_server();
    /*
     * Install a function to be used when a document has not been found (404
     * error).  The function will be used regardless of its match_strength.
     */
    void install_not_found_function(request_function_ptr&& fn);
}

#endif

