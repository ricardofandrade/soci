//
// Copyright (C) 2004-2006 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci.h"
#include "soci-postgresql.h"
#include "common-tests.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>
#include <cmath>
#include <ctime>
#include <cstdlib>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = postgresql;

// Postgres-specific tests

struct oid_table_creator : public table_creator_base
{
    oid_table_creator(session& sql)
    : table_creator_base(sql)
    {
        sql << "create table soci_test ("
                " id integer,"
                " name varchar(100)"
                ") with oids";
    }
};

// ROWID test
// Note: in PostgreSQL, there is no ROWID, there is OID.
// It is still provided as a separate type for "portability",
// whatever that means.
void test1()
{
    {
        session sql(backEnd, connectString);

        oid_table_creator tableCreator(sql);

        sql << "insert into soci_test(id, name) values(7, \'John\')";

        rowid rid(sql);
        sql << "select oid from soci_test where id = 7", into(rid);

        int id;
        std::string name;

#ifndef SOCI_PGSQL_NOPARAMS

        sql << "select id, name from soci_test where oid = :rid",
            into(id), into(name), use(rid);

#else
        // Older PostgreSQL does not support use elements.

        postgresql_rowid_backend *rbe
            = static_cast<postgresql_rowid_backend *>(rid.get_backend());

        unsigned long oid = rbe->value_;

        sql << "select id, name from soci_test where oid = " << oid,
            into(id), into(name);

#endif // SOCI_PGSQL_NOPARAMS

        assert(id == 7);
        assert(name == "John");
    }

    std::cout << "test 1 passed" << std::endl;
}

// function call test
class function_creator : function_creator_base
{
public:

    function_creator(session& session)
    : function_creator_base(session)
    {
        // before a language can be used it must be defined
        // if it has already been defined then an error will occur
        try { session << "create language plpgsql"; }
        catch (soci_error const &) {} // ignore if error

#ifndef SOCI_PGSQL_NOPARAMS

        session  <<
            "create or replace function soci_test(msg varchar) "
            "returns varchar as $$ "
            "begin "
            "  return msg; "
            "end $$ language plpgsql";
#else

       session <<
            "create or replace function soci_test(varchar) "
            "returns varchar as \' "
            "begin "
            "  return $1; "
            "end \' language plpgsql";
#endif
    }

protected:

    std::string drop_statement()
    {
        return "drop function soci_test(varchar)";
    }
};

void test2()
{
    {
        session sql(backEnd, connectString);

        function_creator functionCreator(sql);

        std::string in("my message");
        std::string out;

#ifndef SOCI_PGSQL_NOPARAMS

        statement st = (sql.prepare <<
            "select soci_test(:input)",
            into(out),
            use(in, "input"));

#else
        // Older PostgreSQL does not support use elements.

        statement st = (sql.prepare <<
            "select soci_test(\'" << in << "\')",
            into(out));

#endif // SOCI_PGSQL_NOPARAMS

        st.execute(true);
        assert(out == in);

        // explicit procedure syntax
        {
            std::string in("my message2");
            std::string out;

#ifndef SOCI_PGSQL_NOPARAMS

            procedure proc = (sql.prepare <<
                "soci_test(:input)",
                into(out), use(in, "input"));

#else
        // Older PostgreSQL does not support use elements.

            procedure proc = (sql.prepare <<
                "soci_test(\'" << in << "\')", into(out));

#endif // SOCI_PGSQL_NOPARAMS

            proc.execute(true);
            assert(out == in);
        }
    }

    std::cout << "test 2 passed" << std::endl;
}

// BLOB test
struct blob_table_creator : public table_creator_base
{
    blob_table_creator(session& session)
    : table_creator_base(session)
    {
        session <<
             "create table soci_test ("
             "    id integer,"
             "    img oid"
             ")";
    }
};

// long long test
void test3()
{
    {
        session sql(backEnd, connectString);

        blob_table_creator tableCreator(sql);
        
        char buf[] = "abcdefghijklmnopqrstuvwxyz";

        sql << "insert into soci_test(id, img) values(7, lo_creat(-1))";

        // in PostgreSQL, BLOB operations must be within transaction block
        transaction tr(sql);

        {
            blob b(sql);

            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == 0);

            b.write(0, buf, sizeof(buf));
            assert(b.get_len() == sizeof(buf));

            b.append(buf, sizeof(buf));
            assert(b.get_len() == 2 * sizeof(buf));
        }
        {
            blob b(sql);
            sql << "select img from soci_test where id = 7", into(b);
            assert(b.get_len() == 2 * sizeof(buf));
            char buf2[100];
            b.read(0, buf2, 10);
            assert(strncmp(buf2, "abcdefghij", 10) == 0);
        }

        unsigned long oid;
        sql << "select img from soci_test where id = 7", into(oid);
        sql << "select lo_unlink(" << oid << ")";
    }

    std::cout << "test 3 passed" << std::endl;
}

struct longlong_table_creator : table_creator_base
{
    longlong_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val int8)";
    }
};

void test4()
{
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        long long v1 = 1000000000000LL;
        assert(v1 / 1000000 == 1000000);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        long long v2 = 0LL;
        sql << "select val from soci_test", into(v2);

        assert(v2 == v1);
    }

    std::cout << "test 4 passed" << std::endl;
}

struct boolean_table_creator : table_creator_base
{
    boolean_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val boolean)";
    }
};

void test5()
{
    {
        session sql(backEnd, connectString);

        boolean_table_creator tableCreator(sql);

        int i1 = 0;

        sql << "insert into soci_test(val) values(:val)", use(i1);

        int i2 = 7;
        sql << "select val from soci_test", into(i2);

        assert(i2 == i1);

        sql << "update soci_test set val = true";
        sql << "select val from soci_test", into(i2);
        assert(i2 == 1);
    }

    std::cout << "test 5 passed" << std::endl;
}

// DDL Creation objects for common tests
struct table_creator_one : public table_creator_base
{
    table_creator_one(session& session)
        : table_creator_base(session)
    {
        session << "create table soci_test(id integer, val integer, c char, "
                 "str varchar(20), sh int2, ul numeric(20), d float8, "
                 "tm timestamp, i1 integer, i2 integer, i3 integer, "
                 "name varchar(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(session& session)
        : table_creator_base(session)
    {
        session  << "create table soci_test(num_float float8, num_int integer,"
                     " name varchar(20), sometime timestamp, chr char)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(session& session)
        : table_creator_base(session)
    {
        session << "create table soci_test(name varchar(100) not null, "
            "phone varchar(15))";
    }
};

//
// Support for soci Common Tests
//

class test_context : public test_context_base
{
public:
    test_context(backend_factory const &backEnd,
                std::string const &connectString)
        : test_context_base(backEnd, connectString) {}

    table_creator_base* table_creator_1(session& s) const
    {
        return new table_creator_one(s);
    }

    table_creator_base* table_creator_2(session& s) const
    {
        return new table_creator_two(s);
    }

    table_creator_base* table_creator_3(session& s) const
    {
        return new table_creator_three(s);
    }

    std::string to_date_time(std::string const &dateString) const
    {
        return "timestamptz(\'" + dateString + "\')";
    }

};


int main(int argc, char** argv)
{

#ifdef _MSC_VER
    // Redirect errors, unrecoverable problems, and assert() failures to STDERR,
    // instead of debug message window.
    // This hack is required to run asser()-driven tests by Buildbot.
    // NOTE: Comment this 2 lines for debugging with Visual C++ debugger to catch assertions inside.
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif //_MSC_VER

    if (argc == 2)
    {
        connectString = argv[1];
    }
    else
    {
        std::cout << "usage: " << argv[0]
            << " connectstring\n"
            << "example: " << argv[0]
            << " \'connect_string_for_PostgreSQL\'\n";
        return EXIT_FAILURE;
    }

    try
    {
        test_context tc(backEnd, connectString);
        common_tests tests(tc);
        tests.run();

        std::cout << "\nSOCI Postgres Tests:\n\n";

        test1();
        test2();
        test3();
        test4();
        test5();

        std::cout << "\nOK, all tests passed.\n\n";
        return EXIT_SUCCESS;
    }
    catch (std::exception const & e)
    {
        std::cout << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
