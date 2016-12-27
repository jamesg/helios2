#ifndef SLIDE_HPP
#define SLIDE_HPP

#include <cstring>
#include <list>
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "jsmn.h"

namespace slide
{
    /*
     * Escape a string which may contain quotes to make it suitable for JSON encoding.
     */
    std::string escape(const std::string&);
    /*
     * Unescape a string from JSON.
     */
    std::string unescape(const std::string&);
    /*
     * Helper class to construct a std::ostringstream, stream data to it, and
     * convert it to a string.
     *
     * mkstr objects are implicitly convertable to strings.  Any type that can
     * be streamed to a std::ostringstream can be streamed to an mkstr.
     *
     * Example, writes the string "A1" to s:
     *
     * std::string s = (mkstr() << "A" << 1);
     */
    class mkstr
    {
        public:
            template<typename Output>
            mkstr& operator<<(const Output& output)
            {
                m_oss << output;
                return *this;
            }
            operator std::string() const
            {
                return str();
            }
            std::string str() const
            {
                return m_oss.str();
            }
        private:
            std::ostringstream m_oss;
    };

    /*
     * Base class for all Slide exceptions.
     */
    class exception :
        public std::runtime_error
    {
        public:
            exception(const std::string& message) :
                std::runtime_error(message)
            {
            }
    };

    namespace detail
    {
        //
        // SET THE VALUE OF AN ATTRIBUTE BASED ON A VALUE READ FROM JSON DATA.
        //

        void set_value(bool value, bool& out);
        void set_value(double value, bool& out);
        void set_value(int value, bool& out);
        void set_value(std::string value, bool& out);

        void set_value(bool value, double& out);
        void set_value(double value, double& out);
        void set_value(int value, double& out);
        void set_value(std::string value, double& out);

        void set_value(bool value, int& out);
        void set_value(double value, int& out);
        void set_value(int value, int& out);
        void set_value(std::string value, int& out);

        void set_value(bool value, std::string& out);
        void set_value(double value, std::string& out);
        void set_value(int value, std::string& out);
        void set_value(std::string value, std::string& out);

        //
        // CONVERT AN ATOMIC VALUE TO A JSON STRING.
        //

        void json_str(bool b, std::ostringstream& oss);
        void json_str(double d, std::ostringstream& oss);
        void json_str(int d, std::ostringstream& oss);
        void json_str(std::string str, std::ostringstream& oss);

        //
        // BIND VALUES TO A SQLITE STATEMENT.
        //

        void bind_value(bool value, std::size_t index, sqlite3_stmt *stmt);
        void bind_value(double value, std::size_t index, sqlite3_stmt *stmt);
        void bind_value(int value, std::size_t index, sqlite3_stmt *stmt);
        void bind_value(std::string value, std::size_t index, sqlite3_stmt *stmt);

        template<std::size_t I = 0, typename... Types>
        inline typename std::enable_if<I == sizeof...(Types)>::type
        bind_values(std::tuple<Types...> /*values*/, sqlite3_stmt */*stmt*/)
        {
            // Base case.
        }

        template<std::size_t I = 0, typename... Types>
        inline typename std::enable_if<I < sizeof...(Types)>::type
        bind_values(std::tuple<Types...> values, sqlite3_stmt *stmt)
        {
            bind_value(std::get<I>(values), I + 1, stmt);
            bind_values<I + 1, Types...>(values, stmt);
        }

        //
        // GET THE VALUE OF A COLUMN FROM A SQLITE RESULT SET.
        //

        void get_column(sqlite3_stmt *stmt, std::size_t index, bool& value);
        void get_column(sqlite3_stmt *stmt, std::size_t index, double& value);
        void get_column(sqlite3_stmt *stmt, std::size_t index, int& value);
        void get_column(sqlite3_stmt *stmt, std::size_t index, std::string& value);

        template<std::size_t I = 0, typename... Types>
        inline typename std::enable_if<I == sizeof...(Types)>::type
        get_columns(sqlite3_stmt*, std::tuple<Types...>&)
        {
        }

        template<std::size_t I = 0, typename... Types>
        inline typename std::enable_if<I < sizeof...(Types)>::type
        get_columns(sqlite3_stmt *stmt, std::tuple<Types...>& values)
        {
            get_column(stmt, I, std::get<I>(values));
            get_columns<I + 1, Types...>(stmt, values);
        }
    }

    class query_parameters_base
    {
        public:
            virtual void bind_to(sqlite3_stmt*) const = 0;
    };

    /*
     * A representation of a SQLite row.
     */
    template<typename ...Types>
    class row : public query_parameters_base
    {
        public:
            template<typename ...ColTypes> friend class collection;
            template<typename ...Types2> friend class row;

            /*
             * Make a row from a sequence of values.
             */
            static row<Types...> make_row(Types... params)
            {
                row out;
                out.m_tuple = std::make_tuple(params...);
                return out;
            }

            /*
             * Create a new row that consists of this row concatenated with
             * another in the order [this row, argument row].
             */
            template <typename ...Types2>
            row<Types..., Types2...> cat(row<Types2...> b)
            {
                row<Types..., Types2...> out;
                out.m_tuple = std::tuple_cat(m_tuple, b.m_tuple);
                return out;
            }

            /*
             * Create a new row by parsing a JSON encoded object.
             */
            template<const char *...Attributes>
            static row<Types...> from_json(const std::string& json)
            {
                static_assert(
                        sizeof...(Types) == sizeof...(Attributes),
                        "Length of types list must equal length of attributes list."
                        );
                row<Types...> out;
                jsmn_parser parser;
                jsmn_init(&parser);

                jsmntok_t tokens[256];
                int token_i = 0;
                const char *js = json.c_str();
                jsmn_parse(&parser, js, ::strlen(js), tokens, 256);

                out.parse_object<Attributes...>(js, tokens, token_i);
                return out;
            }
            /*
             * Get the std::tuple backing this row.  The std::tuple can be
             * modified to change the values of the row.
             */
            std::tuple<Types...>& std_tuple()
            {
                return m_tuple;
            }
            const std::tuple<Types...>& std_tuple() const
            {
                return m_tuple;
            }
            /*
             * Bind the values in this row, in order (starting with the first
             * parameter), to a SQLite statement.
             */
            void bind_to(sqlite3_stmt *stmt) const override
            {
                detail::bind_values(m_tuple, stmt);
            }
            /*
             * Get an element in the row by its index (starting at 0).
             */
            template<std::size_t I>
            typename std::tuple_element<I, std::tuple<Types...>>::type& get()
            {
                return std::get<I>(m_tuple);
            }
            template<std::size_t I>
            const typename std::tuple_element<I, std::tuple<Types...>>::type& get() const
            {
                return std::get<I>(m_tuple);
            }
            /*
             * Convert a tuple to a JSON object using attributes given as template
             * parameters.
             */
            template<const char *...Attributes>
            std::string to_json() const
            {
                static_assert(
                        sizeof...(Types) == sizeof...(Attributes),
                        "Length of types list must equal length of attributes list."
                        );
                std::ostringstream oss;
                oss << "{ ";
                write_json_attrs<0, Attributes...>(oss);
                oss << " }";
                return oss.str();
            }

        private:
            //
            // SET THE VALUE OF AN ATTRIBUTE WHERE THE KEY MATCHES.
            //

            template<typename In, std::size_t I, const char *Attr>
            typename std::enable_if<I == sizeof...(Types) - 1>::type
            set(const std::string& key, const In& value)
            {
                if(key == std::string(Attr))
                    detail::set_value(value, get<I>());
            }

            template<typename In, std::size_t I, const char *Attr1,
                const char *Attr2, const char *...Attributes>
            typename std::enable_if<I < sizeof...(Types) - 1>::type
            set(const std::string& key, const In& value)
            {
                if(key == std::string(Attr1))
                    detail::set_value(value, get<I>());
                set<In, I + 1, Attr2, Attributes...>(key, value);
            }

            template<const char *...Attributes>
            void parse_object(const char *js, jsmntok_t *tokens, int& token_i)
            {
                if(tokens[token_i].type == JSMN_OBJECT)
                {
                    jsmntok_t *object_token = (tokens + token_i);
                    for(int i = 0; i < object_token->size; ++i)
                    {
                        // Move to the key.
                        ++token_i;
                        const std::string key_(
                                js + tokens[token_i].start, js + tokens[token_i].end
                                );
                        const std::string key = unescape(key_);
                        // Move to the value.
                        ++token_i;
                        switch(tokens[token_i].type)
                        {
                            case JSMN_PRIMITIVE:
                                if(
                                        *(js + tokens[token_i].start) == '-' || (
                                            *(js + tokens[token_i].start) >= '0' &&
                                            *(js + tokens[token_i].start) <= '9'
                                            )
                                  )
                                {
                                    double value = std::stod(
                                        std::string(js + tokens[token_i].start, js + tokens[token_i].end)
                                        );
                                    set<double, 0, Attributes...>(key, value);
                                }
                                else if(*(js + tokens[token_i].start) == 't')
                                {
                                    set<bool, 0, Attributes...>(key, true);
                                }
                                else if(*(js + tokens[token_i].start) == 'f')
                                {
                                    set<bool, 0, Attributes...>(key, false);
                                }
                                break;
                            case JSMN_STRING:
                                {
                                    const std::string value(
                                            js + tokens[token_i].start,
                                            js + tokens[token_i].end
                                            );
                                    set<std::string, 0, Attributes...>(key, unescape(value));
                                }
                                break;
                            case JSMN_ARRAY:
                            case JSMN_OBJECT:
                                // TODO
                                break;
                        }
                    }
                }
            }

            //
            // WRITE A JSON ATTRIBUTE LIST (THE INSIDE OF AN OBJECT).
            //
            // Write one type - attribute pair at a time.
            //

            template<std::size_t I, const char *Attr1,
                const char *Attr2, const char *...Attributes>
            typename std::enable_if<I + 1 < sizeof...(Types)>::type
            write_json_attrs(std::ostringstream& oss) const
            {
                // Inductive case.
                detail::json_str(std::string(Attr1), oss);
                oss << ": ";
                detail::json_str(get<I>(), oss);
                oss << ", ";
                write_json_attrs<I + 1, Attr2, Attributes...>(oss);
            }

            template<std::size_t I, const char *Attr1>
            typename std::enable_if<I + 1 == sizeof...(Types)>::type
            write_json_attrs(std::ostringstream& oss) const
            {
                // Base case.
                detail::json_str(std::string(Attr1), oss);
                oss << ": ";
                detail::json_str(get<I>(), oss);
            }

            std::tuple<Types...> m_tuple;
    };

    namespace detail
    {
        /*
         * Retrieve values from a SQLite statement.
         */
        template<typename ...Types>
        row<Types...> get_row(sqlite3_stmt *stmt)
        {
            row<Types...> out;
            get_columns(stmt, out.std_tuple());
            return out;
        }
    }

    /*
     * A representation of a SQLite result set.
     */
    template<typename ...Types>
    class collection
    {
        public:
            typedef row<Types...> row_type;
            typedef typename std::vector<row_type> internal_type;

            collection()
            {
            }
            collection(const collection& c) :
                m_vector(c.m_vector)
            {
            }
            collection(collection&& c) :
                m_vector(c.m_vector)
            {
            }
            collection(std::initializer_list<row_type> init) :
                m_vector(init)
            {
            }

            typename internal_type::iterator begin()
            {
                return m_vector.begin();
            }
            typename internal_type::const_iterator begin() const
            {
                return m_vector.begin();
            }
            typename internal_type::const_iterator cbegin() const
            {
                return m_vector.cbegin();
            }
            typename internal_type::iterator end()
            {
                return m_vector.end();
            }
            typename internal_type::const_iterator end() const
            {
                return m_vector.end();
            }
            typename internal_type::const_iterator cend() const
            {
                return m_vector.cend();
            }
            row_type& at(std::size_t i)
            {
                return m_vector.at(i);
            }
            const row_type& at(std::size_t i) const
            {
                return m_vector.at(i);
            }
            void push_back(const row_type& r)
            {
                m_vector.push_back(r);
            }
            std::size_t size() const
            {
                return m_vector.size();
            }

            template<const char * ...Attributes>
            static collection<Types...> from_json(const std::string json)
            {
                static_assert(
                        sizeof...(Types) == sizeof...(Attributes),
                        "Length of types list must equal length of attributes list."
                        );
                collection<Types...> out;
                jsmn_parser parser;
                jsmn_init(&parser);

                jsmntok_t tokens[256];
                int token_i = 0;
                const char *js = json.c_str();
                jsmn_parse(&parser, js, ::strlen(js), tokens, 256);

                if(tokens[0].type == JSMN_ARRAY)
                {
                    for(int i = 0; i < tokens[0].size; ++i)
                    {
                        ++token_i;
                        row_type r;
                        r.template parse_object<Attributes...>(js, tokens, token_i);
                        out.push_back(r);
                    }
                }
                return out;
            }

            template<const char * ...Attributes>
            std::string to_json()
            {
                static_assert(
                        sizeof...(Types) == sizeof...(Attributes),
                        "Length of types list must equal length of attributes list."
                        );
                std::ostringstream oss;
                oss << "[ ";
                for(std::size_t i = 0; i < size(); ++i)
                {
                    oss << at(i).template to_json<Attributes...>();
                    if(i != size() - 1)
                        oss << ", ";
                }
                oss << " ]";
                return oss.str();
            }

            template<std::size_t I>
            void set_attr(const typename std::tuple_element<I, std::tuple<Types...>>::type& value)
            {
                //for(row<Types...>& r : *this)
                    //r.get<I>() = value;
                for(std::size_t i = 0; i < size(); ++i)
                    at(i).template get<I>() = value;
            }
        private:
            internal_type m_vector;
    };

    class connection;
    class transaction;
    /*
     * Execute a devoid query.  The query has no parameters and does not return
     * a result set.
     *
     * Return the number of rows changed.
     */
    int devoid(const std::string& query, connection& db);

    class connection
    {
    public:
        /*
         * Open a connection to a new in-memory database.
         */
        static connection in_memory_database()
        {
            connection c(":memory:");
            return c;
        }
        /*
         * Attempt to open a connection to the database file.  Throws an
         * exception if the connection could not be established.
         */
        connection(std::string filename) :
            m_handle(nullptr)
        {
            auto err = sqlite3_open(filename.c_str(), &m_handle);
            if(err == SQLITE_OK)
            {
                enable_foreign_keys();
            }
            else
            {
                if(m_handle)
                {
                    sqlite3_close(m_handle);
                    m_handle = nullptr;
                }
                throw exception("Unable to open SQLite connection");
            }

        }
        /*
         * Move a SQLite connection.  The old connection object will no longer
         * be connected to a database.
         */
        connection(connection&& o) :
            m_handle(o.m_handle)
        {
            o.m_handle = nullptr;
        }

        ~connection()
        {
            if(m_handle != nullptr)
            {
                sqlite3_close(m_handle);
            }
        }
        /*
         * The internal SQLite handle representing the database connection.
         * Required for running custom queries not assisted by this class.
         */
        sqlite3 *handle()
        {
            if(m_handle == nullptr)
                throw exception("handle is null");
            return m_handle;
        }

        std::size_t transaction_depth()
        {
            return m_transactions.size();
        }

        void enter_transaction(transaction *t)
        {
            m_transactions.push_front(t);
        }

        void finish_transaction()
        {
            m_transactions.pop_front();
        }
        /*
         * Get the transaction below the current one.
         *
         * Used for rolling back to a previous transaction state by name.
         */
        transaction *peek2_transaction()
        {
            auto it = m_transactions.begin();
            if(it != m_transactions.end())
                ++it;
            if(it != m_transactions.end())
                return *it;
            return nullptr;
        }
    private:
        void enable_foreign_keys()
        {
            devoid("PRAGMA foreign_keys = ON", *this);
        }

        sqlite3 *m_handle;
        // Stack of transactions open on the database connection.
        std::list<transaction*> m_transactions;
    };

    /*
     * Execute a devoid query.  The query may have parameters, but does not
     * have a result set.
     *
     * Return the number of rows changed.
     */
    class transaction
    {
    public:
        /*
         * Open a new transaction with a named savepoint (so it can be rolled
         * back or committed in the destructor).
         */
        transaction(connection& conn, const std::string& name) :
            m_connection(conn),
            m_savepoint_name(name),
            m_released(false)
        {
            // Start a SQLite TRANSACTION if outside of a transaction.
            if(m_connection.transaction_depth() == 0)
            {
                devoid("BEGIN TRANSACTION", m_connection);
#ifdef SLIDE_ENABLE_DEBUGGING
                std::cerr << "BEGIN TRANSACTION" << std::endl;
#endif
            }

            devoid(
                    mkstr() << "SAVEPOINT " << m_savepoint_name,
                    m_connection
                    );
#ifdef SLIDE_ENABLE_DEBUGGING
            std::cerr << "SAVEPOINT " << m_savepoint_name << std::endl;
#endif

            m_connection.enter_transaction(this);
        }
        ~transaction()
        {
            if(!m_released)
            {
                try
                {
                    rollback();
                }
                catch(const std::exception& e)
                {
                }
            }
        }
        void commit()
        {
#ifdef SLIDE_ENABLE_DEBUGGING
            std::cerr << "commit transaction" << std::endl;
#endif

            if(m_released)
            {
#ifdef SLIDE_ENABLE_DEBUGGING
                std::cerr << "savepoint already released" << std::endl;
#endif
                throw exception("Savepoint already released");
            }

            devoid(mkstr() << "RELEASE SAVEPOINT " << m_savepoint_name, m_connection);
#ifdef SLIDE_ENABLE_DEBUGGING
            std::cerr << "RELEASE SAVEPOINT " << m_savepoint_name << std::endl;
#endif

            m_connection.finish_transaction();
            m_released = true;

            if(m_connection.transaction_depth() == 0)
            {
                devoid("COMMIT TRANSACTION", m_connection);
#ifdef SLIDE_ENABLE_DEBUGGING
                std::cerr << "COMMIT TRANSACTION" << std::endl;
#endif
            }
        }
        void rollback()
        {
            if(m_released)
                throw exception("Savepoint already released");

            devoid(
                mkstr() << "ROLLBACK TO SAVEPOINT " <<
                    m_savepoint_name,
                m_connection
                );
#ifdef SLIDE_ENABLE_DEBUGGING
            std::cerr << "ROLLBACK TO SAVEPOINT " <<
                m_savepoint_name << std::endl;
#endif

            m_connection.finish_transaction();
            m_released = true;

            if(m_connection.transaction_depth() == 0)
            {
                devoid("ROLLBACK TRANSACTION", m_connection);
#ifdef SLIDE_ENABLE_DEBUGGING
                std::cerr << "ROLLBACK TRANSACTION" << std::endl;
#endif
            }
        }
    private:
        connection& m_connection;
        std::string m_savepoint_name;
        bool m_released;
    };
    /*
     * Step a SQLite statement, converting error codes into exceptions.
     */
    int step(sqlite3_stmt *stmt);
    template <typename ...Types>
    int devoid(const std::string& query, const row<Types...>& values, connection& db)
    {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare(db.handle(), query.c_str(), -1, &stmt, nullptr);
        if(stmt == nullptr)
            throw exception(
                mkstr() << "preparing SQL statement \"" << query << "\": " <<
                    sqlite3_errmsg(db.handle())
            );
        detail::bind_values(values.std_tuple(), stmt);
        //int step_ret = sqlite3_step(stmt);
        step(stmt);
        int finalise_ret = sqlite3_finalize(stmt);
        //if(step_ret != SQLITE_DONE)
            //throw exception(
                //mkstr() << "stepping devoid SQL query \"" << query <<
                    //"\": " << sqlite3_errmsg(db.handle())
                //);
        if(finalise_ret != SQLITE_OK)
            throw exception(
                mkstr() << "finalising devoid SQL query \"" << query <<
                    "\": " << sqlite3_errmsg(db.handle())
                );
        return sqlite3_changes(db.handle());
    }

    /*
     * Execute a devoid query for each set of parameters in a collection.
     *
     * Return the number of rows changed.
     */
    template <typename ...Types>
    void devoid(const std::string& query, const collection<Types...>& values, connection& conn)
    {
        transaction tr(conn, "slide_devoid");
        for(row<Types...> r : values)
            try
            {
                devoid(query, r, conn);
            }
            catch(const exception&)
            {
                tr.rollback();
                return;
            }
        tr.commit();
    }

    template<typename ...Types>
    row<Types...> get_row(
            connection &conn,
            std::string query,
            const query_parameters_base& v
            )
    {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare(conn.handle(), query.c_str(), -1, &stmt, nullptr);
        if(stmt == nullptr)
            throw exception(
                mkstr() << "preparing SQL statement \"" << query << "\": " <<
                    sqlite3_errmsg(conn.handle())
            );
        v.bind_to(stmt);

        row<Types...> out;

        if(step(stmt) == SQLITE_ROW)
        {
            try
            {
                out = detail::get_row<Types...>(stmt);
            }
            catch(const std::exception&)
            {
                sqlite3_finalize(stmt);
                throw;
            }
        }
        else
        {
            throw exception("no rows returned");
        }

        if(sqlite3_finalize(stmt) != SQLITE_OK)
            throw exception("finalizing SQLite statement");

        return out;
    }
    template<typename ...Types>
    row<Types...> get_row(
            connection &conn,
            std::string query
            )
    {
        return get_row<Types...>(conn, query, row<>());
    }

    template<typename ...Types>
    collection<Types...> get_collection(
            connection &conn,
            std::string query,
            const query_parameters_base& v
            )
    {
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare(conn.handle(), query.c_str(), -1, &stmt, nullptr);
        if(stmt == nullptr)
            throw exception(
                mkstr() << "preparing SQL statement \"" << query << "\": " <<
                    sqlite3_errmsg(conn.handle())
            );
        v.bind_to(stmt);

        collection<Types...> out;

        while(step(stmt) == SQLITE_ROW)
        {
            try
            {
                row<Types...> row_ = detail::get_row<Types...>(stmt);
                out.push_back(row_);
            }
            catch(const std::exception&)
            {
                sqlite3_finalize(stmt);
                throw;
            }
        }

        if(sqlite3_finalize(stmt) != SQLITE_OK)
            throw exception("finalizing SQLite statement");

        return out;
    }
    template<typename ...Types>
    collection<Types...> get_collection(
            connection &conn,
            std::string query
            )
    {
        return get_collection<Types...>(conn, query, row<>());
    }
    /*
     * Get the id of the last inserted row.  This will be the primary key for
     * any relvar that has one.
     */
    int last_insert_rowid(connection&);
}

#endif

