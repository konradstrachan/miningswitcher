#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <string>

class CTextFileDB
{
public:
    explicit CTextFileDB( const std::string& strPathToDBFile )
        : m_strPathToDBFile( strPathToDBFile )
    {
        std::ifstream file( strPathToDBFile );
        std::string strBuffer;
        while( getline( file,strBuffer ) ) 
        {
            if(!strBuffer.empty() && strBuffer[0] == '#')
            {
                // This is a comment
                continue;
            }

            // Find delim
            const std::string delim("=");

            std::string::size_type nPos = strBuffer.find( delim );
            if( nPos != std::string::npos )
            {
                m_mapDB[ strBuffer.substr( 0, nPos ) ] =  strBuffer.substr( nPos + delim.size(), strBuffer.size() );
                m_vecDBOrder.push_back( strBuffer.substr( 0, nPos ) );
            }
        }

        file.close();
    }

    ~CTextFileDB()
    {
        std::ofstream file( m_strPathToDBFile, std::ios::out );
        DataBaseOrder::const_iterator it = m_vecDBOrder.begin();
        DataBaseOrder::const_iterator itEnd = m_vecDBOrder.end();

        for( ; it != itEnd ; ++it )
        {
            file << *it << "||" << m_mapDB[ *it ] << std::endl;
        }

        file.close();
    }

    void ModifyEntry( const std::string& strKey, int nValue )
    {
        char szBuffer[64];
        _itoa_s( nValue, szBuffer, 64, 10 );
        ModifyEntry( strKey, std::string( szBuffer ) );
    }

    void ModifyEntry( const std::string& strKey, const std::string& strValue )
    {
        bool fFound = false;
        DataBaseOrder::const_iterator it = m_vecDBOrder.begin();
        DataBaseOrder::const_iterator itEnd = m_vecDBOrder.end();

        for( ; it != itEnd ; ++it )
        {
            if( *it == strKey )
            {
                fFound = true;
                break;
            }
        }

        if( ! fFound )
        {
            m_vecDBOrder.push_back( strKey );
        }

        m_mapDB[ strKey ] = strValue;
    }

    const std::string& GetEntry( const std::string& strKey )
    {
        return m_mapDB[ strKey ];
    }

private:

    typedef std::vector< std::string > DataBaseOrder;
    typedef std::map< std::string, std::string > DataBase;

    DataBaseOrder m_vecDBOrder;
    DataBase m_mapDB;

    std::string m_strPathToDBFile;
};

// test code
//CTextFileDB test( "C:\\testdb.txt" );
//test.ModifyEntry( "ABC", "123" );
//test.ModifyEntry( "CDE", "456" );
//test.ModifyEntry( "Numeric", 1235678333 );
//test.ModifyEntry( "Empty", "" );