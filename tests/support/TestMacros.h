#ifndef ESPI_TEST_MACROS_H
#define ESPI_TEST_MACROS_H

// Minimal assertion macros for core tests.
//
// Deliberately not the SDK's testlib TestMacros.h: core tests must build with
// no Saleae headers on the include path at all, which is what keeps the core
// honest about its dependencies. Integration tests (T2/T3) link the harness
// and may use its macros instead.

#include <cstdio>
#include <cstdlib>
#include <string>

namespace espi_test
{

inline int& FailureCount()
{
    static int count = 0;
    return count;
}

inline std::string Hex( unsigned long long v, int digits = 2 )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof( buf ), "0x%0*llX", digits, v );
    return buf;
}

} // namespace espi_test

#define TEST_CHECK( cond )                                                                                                         \
    do                                                                                                                             \
    {                                                                                                                              \
        if( !( cond ) )                                                                                                            \
        {                                                                                                                          \
            std::fprintf( stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond );                                                \
            ++::espi_test::FailureCount();                                                                                         \
        }                                                                                                                          \
    } while( 0 )

#define TEST_CHECK_EQ( actual, expected )                                                                                          \
    do                                                                                                                             \
    {                                                                                                                              \
        auto _a = ( actual );                                                                                                      \
        auto _e = ( expected );                                                                                                    \
        if( !( _a == _e ) )                                                                                                        \
        {                                                                                                                          \
            std::fprintf( stderr, "FAIL  %s:%d  %s\n        expected %s\n        got      %s\n", __FILE__, __LINE__, #actual,      \
                          ::espi_test::Hex( (unsigned long long)_e ).c_str(),                                                      \
                          ::espi_test::Hex( (unsigned long long)_a ).c_str() );                                                    \
            ++::espi_test::FailureCount();                                                                                         \
        }                                                                                                                          \
    } while( 0 )

#define TEST_MAIN_RETURN()                                                                                                         \
    do                                                                                                                             \
    {                                                                                                                              \
        if( ::espi_test::FailureCount() != 0 )                                                                                     \
        {                                                                                                                          \
            std::fprintf( stderr, "\n%d check(s) failed\n", ::espi_test::FailureCount() );                                         \
            return EXIT_FAILURE;                                                                                                   \
        }                                                                                                                          \
        std::printf( "ok\n" );                                                                                                     \
        return EXIT_SUCCESS;                                                                                                       \
    } while( 0 )

#endif // ESPI_TEST_MACROS_H
