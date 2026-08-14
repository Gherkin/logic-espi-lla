#include "espi/Decode.h"

#include <sstream>

namespace espi
{
namespace
{

const Field* FindIn( const std::vector<Field>& fields, const std::string& name )
{
    for( const Field& f : fields )
    {
        if( f.name == name )
            return &f;
        if( const Field* hit = f.Find( name ) )
            return hit;
    }
    return nullptr;
}

bool AnyError( const std::vector<Field>& fields )
{
    for( const Field& f : fields )
    {
        if( f.severity == Severity::Error )
            return true;
        if( AnyError( f.children ) )
            return true;
    }
    return false;
}

const char* Marker( Severity s )
{
    switch( s )
    {
    case Severity::Warning:
        return " [!]";
    case Severity::Error:
        return " [ERROR]";
    case Severity::Info:
    default:
        return "";
    }
}

} // namespace

const Field* Field::Find( const std::string& n ) const
{
    return FindIn( children, n );
}

const Field* Transaction::Find( const std::string& name ) const
{
    return FindIn( fields, name );
}

bool Transaction::HasError() const
{
    return AnyError( fields );
}

std::string Render( const Field& field, int indent )
{
    std::ostringstream out;
    out << std::string( static_cast<size_t>( indent ) * 2, ' ' ) << field.name;
    if( !field.text.empty() )
        out << "  " << field.text;
    out << Marker( field.severity ) << '\n';

    for( const Field& child : field.children )
        out << Render( child, indent + 1 );

    return out.str();
}

std::string Render( const Transaction& transaction )
{
    std::ostringstream out;
    for( const Field& f : transaction.fields )
        out << Render( f, 0 );
    if( transaction.truncated )
        out << "TRUNCATED  chip select deasserted mid-packet [ERROR]\n";
    return out.str();
}

} // namespace espi
