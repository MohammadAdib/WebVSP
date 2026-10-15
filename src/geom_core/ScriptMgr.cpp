//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// ScriptMgr.h: interface to AngelScript
// J.R Gloudemans
//
// Note: For consistency when adding Doxygen comments, follow the Qt style
// identified here: http://www.doxygen.nl/manual/docblocks.html#specialblock
//
//
//////////////////////////////////////////////////////////////////////

#include "ScriptMgr.h"
#include "ScriptCall.h"

#include "Parm.h"
#include "Matrix4d.h"
#include "VSP_Geom_API.h"
#include "APIErrorMgr.h"
#include "CustomGeom.h"
#include "AdvLinkMgr.h"
#include "AttributeManager.h"
#include "StringUtil.h"
#include "FileUtil.h"

// Make sure int32_t is defined.
#ifdef _MSC_VER
    #if _MSC_VER >= 1600
        #include <cstdint>
    #else
        typedef __int32 int32_t;
    #endif
#else
    #include <cstdint>
#endif

using namespace vsp;

//==== Implement a simple message callback function ====//
void MessageCallback( const asSMessageInfo *msg, void *param )
{
    char str[1024];
    const char *type = "ERR ";
    if( msg->type == asMSGTYPE_WARNING )
    {
        type = "WARN";
    }
    else if( msg->type == asMSGTYPE_INFORMATION )
    {
        type = "INFO";
    }

    //==== Check if MessageCallback is related to the AdvLink scripting ====//
    string msg_str = string( msg->section );
    int row_offset = 0;

    string advlink_label = string( "AdvLink_" );
    if ( strstr( msg->section, advlink_label.c_str() ) )
    {
        // Adjust row offset based on metadata sent with msg->section
        int num_start = advlink_label.size();
        string sub_str = msg_str.substr( num_start, msg_str.size() - num_start );
        int num_end = sub_str.find( '_' );
        string num_str = sub_str.substr( 0, num_end );
        row_offset = stoi( num_str );

        // Remove row adjustment data from message string for clean display
        msg_str = msg_str.substr( 0, num_start ) + msg_str.substr( num_start + num_end + 1, msg_str.size() );
    }

    snprintf( str, sizeof( str ),  "%s (%d, %d) : %s : %s\n", msg_str.c_str(), msg->row - row_offset, msg->col, type, msg->message );
    ScriptMgr.AddToMessages( str );
    printf( "%s", str );
}

//==================================================================================================//
//========================================= ScriptMgr      =========================================//
//==================================================================================================//


//==== Constructor ====//
ScriptMgrSingleton::ScriptMgrSingleton()
{
    m_SaveInt = 0;
    m_ScriptEngine = nullptr;
    m_ScriptMessages = "";

    m_IntArrayType = nullptr;
    m_DoubleArrayType = nullptr;
    m_DoubleMatArrayType = nullptr;
    m_Vec3dArrayType = nullptr;
    m_StringArrayType = nullptr;

}

//==== Set Up Script Engine, Script Error Callbacks ====//
void ScriptMgrSingleton::Init( )
{
    //==== Only Init Once ====//
    static bool init_flag = false;
    if ( init_flag )
        return;
    init_flag = true;

    //==== Create the Script Engine ====//
    m_ScriptEngine = asCreateScriptEngine( ANGELSCRIPT_VERSION );
    asIScriptEngine* se = m_ScriptEngine;

    //==== Set the message callback to receive information on errors in human readable form.  ====//
    int r = se->SetMessageCallback( asFUNCTION( MessageCallback ), 0, asCALL_CDECL );
    assert( r >= 0 );

    //==== Register Addons ====//
    RegisterStdString( m_ScriptEngine );
    RegisterScriptArray( m_ScriptEngine, true );
    RegisterScriptDateTime( m_ScriptEngine );
    RegisterScriptFile( m_ScriptEngine );
    RegisterScriptFileSystem( m_ScriptEngine );
    RegisterStdStringUtils( m_ScriptEngine );
    RegisterScriptMath( m_ScriptEngine );
    RegisterScriptAny( m_ScriptEngine );
    //==== Cache Some Common Types ====//
    m_IntArrayType    = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<int>" ) );
    assert( m_IntArrayType );
    m_IntMatArrayType = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<array<int>>" ) );
    assert( m_IntMatArrayType );
    m_DoubleArrayType = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<double>" ) );
    assert( m_DoubleArrayType );
    m_DoubleMatArrayType = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<array<double>>" ) );
    assert( m_DoubleMatArrayType );
    m_StringArrayType = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<string>" ) );
    assert( m_StringArrayType );

    //==== Register VSP Enums ====//
    RegisterEnums( m_ScriptEngine );

    //==== Register VSP Objects ====//
    RegisterVec3d( m_ScriptEngine );
    m_Vec3dArrayType  = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<vec3d>" ) );
    assert( m_Vec3dArrayType );

    RegisterVec2d( m_ScriptEngine );
    m_Vec2dArrayType  = se->GetTypeInfoById( se->GetTypeIdByDecl( "array<vec2d>" ) );
    assert( m_Vec2dArrayType );

    RegisterMatrix4d( m_ScriptEngine );
    RegisterCustomGeomMgr( m_ScriptEngine );
    RegisterAdvLinkMgr( m_ScriptEngine );
    RegisterAPIErrorObj( m_ScriptEngine );
    RegisterAPI( m_ScriptEngine );
    RegisterUtility(  m_ScriptEngine );
}

void ScriptMgrSingleton::RunTestScripts()
{
    ////===== Run Test Scripts ====//
    //ScriptMgr.ReadScript( "TestScript", "../../TestScript.as"  );
    //ScriptMgr.ReadScriptFromFile( "TestScript", "../../../TestScript.as"  );
    //ScriptMgr.ExecuteScript( "TestScript", "void main()" );
    //ScriptMgr.ExecuteScript( "TestScript", "void TestAPIScript()" );
}

//==== Read And Execute Script File  ====//
int ScriptMgrSingleton::ReadExecuteScriptFile( const string &  file_name, const string &  function_name )
{
    string module_name = ReadScriptFromFile( "ReadExecute", file_name );

    return ExecuteScript( module_name.c_str(), function_name.c_str(), false, 0.0, false );
}

vector< string > ScriptMgrSingleton::ReadScriptsFromDir( const string & dir_name, const string & suffix )
{
    vector< string > mod_name_vec;

    vector< string > file_vec = ScanFolder( dir_name.c_str() );

    for ( int i = 0 ; i < ( int )file_vec.size() ; i++ )
    {
        int s_num = suffix.size();
        if ( file_vec[i].size() > s_num )
        {
            if ( file_vec[i].compare( file_vec[i].size() - s_num, s_num, suffix.c_str() ) == 0 )
            {
                string sub = file_vec[i].substr( 0, file_vec[i].size() - s_num );
                string file_name = dir_name;
                file_name.append( file_vec[i] );
                string module_name = ScriptMgr.ReadScriptFromFile( sub, file_name );

                if ( module_name.size() )
                    mod_name_vec.push_back( module_name );
            }
        }
    }

    return mod_name_vec;
}

//==== Start A New Module And Read Script ====//
string ScriptMgrSingleton::ReadScriptFromFile( const string & module_name, const string &  file_name )
{
    string content = ExtractContent( file_name );

    if ( content.size() < 2 )
    {
        return string();
    }

    return ReadScriptFromMemory( module_name, content );
}

//==== Start A New Module And Read Script ====//
string ScriptMgrSingleton::ReadScriptFromMemory( const string &  module_name, const string & script_content )
{
    int r;
    string updated_module_name = module_name;
    unordered_map< string, string >::iterator iter;

    //==== Check If Module Name Already Exists ====//
    iter = m_ModuleContentMap.find(updated_module_name);
    if ( iter != m_ModuleContentMap.end() )
    {
        //==== Check If Content is Same ====//
        if ( iter->second == script_content )
            return iter->first;

        //==== Need To Change Module Name ====//
        static int dup_cnt = 0;
        updated_module_name.append( StringUtil::int_to_string( dup_cnt, "%d" ) );
        dup_cnt++;
    }

    //==== Make Sure Not Duplicate Of Any Other Module ====//
    for ( iter = m_ModuleContentMap.begin() ; iter != m_ModuleContentMap.end() ; iter++ )
    {
        if ( iter->second == script_content )
            return iter->first;
    }

    //==== Start A New Module ====//
    r = m_ScriptBuilder.StartNewModule( m_ScriptEngine, updated_module_name.c_str() );
    if( r < 0 )        return string();

    r = m_ScriptBuilder.AddSectionFromMemory( updated_module_name.c_str(), script_content.c_str(), script_content.size()  );
    if ( r < 0 )    return string();

    r = m_ScriptBuilder.BuildModule();
    if ( r < 0 )    return string();

    //==== Add To Map ====//
    m_ModuleContentMap[ updated_module_name ] = script_content;

    return updated_module_name;
}

//==== Extract Content From File Into String ====//
string ScriptMgrSingleton::ExtractContent( const string & file_name )
{
    string file_content;
    FILE* fp = fopen( file_name.c_str(), "r" );
    if ( fp )
    {
        char buff[512];
        while ( fgets( buff, 512, fp ) )
        {
            file_content.append( buff );
        }
        file_content.append( "\0" );
        fclose( fp );
    }
    return file_content;
}

//==== Find Script And Remove ====//
bool ScriptMgrSingleton::RemoveScript( const string &  module_name )
{
    //==== Find Module ====//
    unordered_map< string, string >::iterator iter;
    iter = m_ModuleContentMap.find(module_name);
    if ( iter == m_ModuleContentMap.end() )
    {
        return false;                           // Could not find module name;
    }

    m_ModuleContentMap.erase( iter );

    int ret = m_ScriptEngine->DiscardModule( module_name.c_str() );

    if ( ret < 0 )
        return false;
    return true;
}


//==== Execute Function in Module ====//
int ScriptMgrSingleton::ExecuteScript( const char* module_name, const char* function_name, bool arg_flag, double arg, bool by_decl )
{
    // Find the function that is to be called.
    asIScriptModule *mod = m_ScriptEngine->GetModule( module_name );

    if ( !mod )
    {
        printf( "Error ExecuteScript GetModule %s\n", module_name );
        return 1;
    }

    asIScriptFunction *func = nullptr;
    if ( by_decl )
    {
        func = mod->GetFunctionByDecl( function_name );
    }
    else
    {
        func = mod->GetFunctionByName( function_name );
    }

    if( func == 0 )
    {
        return 1;
    }

    // Create our context, prepare it, and then execute
    asIScriptContext *ctx = m_ScriptEngine->CreateContext();
    ctx->Prepare( func );
    if ( arg_flag )
    {
        ctx->SetArgDouble( 0, arg );
    }
    int r = ctx->Execute();
    if( r != asEXECUTION_FINISHED )
    {
        // The execution didn't complete as expected. Determine what happened.
        if( r == asEXECUTION_EXCEPTION )
        {
            // An exception occurred, let the script writer know what happened so it can be corrected.
            printf( "An exception '%s' occurred \n", ctx->GetExceptionString() );
        }
        ctx->Release();
        return 1;
    }

    asDWORD ret = ctx->GetReturnDWord();
    int32_t rval = ret;

    ctx->Release();
    return rval;
}

//==== Return Script Content Given Module Name ====//
string ScriptMgrSingleton::FindModuleContent( const string &  module_name )
{
    unordered_map< string, string >::iterator iter;
    iter = m_ModuleContentMap.find( module_name );

    string file_string;
    if ( iter != m_ModuleContentMap.end() )
    {
        file_string = iter->second;
    }
    return file_string;
}

//==== Write Script Content To File ====//
int ScriptMgrSingleton::SaveScriptContentToFile( const string & module_name, const string & file_name )
{
    unordered_map< string, string >::iterator iter;
    iter = m_ModuleContentMap.find( module_name );

    if ( iter == m_ModuleContentMap.end() )
        return -1;

    FILE* fp = fopen( file_name.c_str(), "w" );
    if ( !fp )
        return -2;

    if ( iter->second.size() == 0 )
        return -3;


    fprintf( fp, "%s", iter->second.c_str() );
    fclose( fp );

    return 0;
}

//==== Find Includes And Replace With Included Code ====//
string ScriptMgrSingleton::ReplaceIncludes( const string & script_contents, const string & inc_file_path )
{
    vector< string::size_type > start_pos_vec;
    vector< string::size_type > end_pos_vec;
    vector< string > file_content_vec;

    string::size_type find_pos = 0;

    string ret_content;

    while ( 1 )
    {
        //==== Find Include ====//
        find_pos = script_contents.find( "#include", find_pos );
        if ( find_pos ==  std::string::npos )
            break;

        string::size_type first_quote = script_contents.find( '"', find_pos + 8 );
        if ( first_quote == std::string::npos )
            break;

        string::size_type second_quote = script_contents.find( '"', first_quote+1 );
        if ( second_quote == std::string::npos )
            break;

        start_pos_vec.push_back( find_pos );
        end_pos_vec.push_back( second_quote );

        string inc_file_name = script_contents.substr( first_quote+1, second_quote - first_quote - 1 );

        string full_path = inc_file_path + inc_file_name;
        string content = ExtractContent( full_path );
        file_content_vec.push_back( content );

        find_pos = second_quote+1;
    }

    //==== No Includes ====//
    if ( file_content_vec.size() == 0 )
        return script_contents;

    string::size_type curr_pos = 0;
    for ( int i = 0 ; i < (int)file_content_vec.size() ; i++ )
    {
        string::size_type s = start_pos_vec[i];
        string::size_type e = end_pos_vec[i];
        ret_content.append( script_contents.substr( curr_pos, s - curr_pos ) );

        ret_content.append( "// Begin Include Replacement\n" );
        ret_content.append( "//" );
        ret_content.append( script_contents, s, e-s+1 );
        ret_content.append( "\n" );
        if ( file_content_vec[i].size() > 0 )
        {
            ret_content.append( file_content_vec[i] );
        }
        ret_content.append( "// End Include Replacement\n" );

        curr_pos = end_pos_vec[i] + 1;
    }

    ret_content.append( script_contents.substr( curr_pos, script_contents.size() - curr_pos ) );

    //FILE * fp = fopen( "TestWrite.txt", "w" );
    //if ( fp )
    //{
    //    fprintf( fp, "%s", ret_content.c_str() );
    //    fclose( fp );
    //};

    return ret_content;
}









//==== Register Enums ====//
void ScriptMgrSingleton::RegisterEnums( asIScriptEngine* se )
{
    // The format for enum comments:
        //! Brief description that appears in overview.
        /*! A more detailed description. */

    int r = se->RegisterEnum( "ABS_REL_FLAG" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ABS_REL_FLAG", "ABS", ABS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ABS_REL_FLAG", "REL", REL );
    assert( r >= 0 );


    r = se->RegisterEnum( "AIRFOIL_EXPORT_TYPE");
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AIRFOIL_EXPORT_TYPE", "SELIG_AF_EXPORT", SELIG_AF_EXPORT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AIRFOIL_EXPORT_TYPE", "BEZIER_AF_EXPORT", BEZIER_AF_EXPORT );
    assert( r >= 0 );


    r = se->RegisterEnum( "ANG_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ANG_UNITS", "ANG_RAD", ANG_RAD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ANG_UNITS", "ANG_DEG", ANG_DEG );
    assert( r >= 0 );


    r = se->RegisterEnum( "ATMOS_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_US_STANDARD_1976", ATMOS_TYPE_US_STANDARD_1976 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_HERRINGTON_1966", ATMOS_TYPE_HERRINGTON_1966 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_MANUAL_P_R", ATMOS_TYPE_MANUAL_P_R );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_MANUAL_P_T", ATMOS_TYPE_MANUAL_P_T );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_MANUAL_R_T", ATMOS_TYPE_MANUAL_R_T );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATMOS_TYPE", "ATMOS_TYPE_MANUAL_RE_L", ATMOS_TYPE_MANUAL_RE_L );
    assert( r >= 0 );


    r = se->RegisterEnum( "ATTACH_TRANS_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_NONE", ATTACH_TRANS_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_COMP", ATTACH_TRANS_COMP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_UV", ATTACH_TRANS_UV );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_RST", ATTACH_TRANS_RST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_LMN", ATTACH_TRANS_LMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_EtaMN", ATTACH_TRANS_EtaMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_TRANS_TYPE", "ATTACH_TRANS_NUM_TYPES", ATTACH_TRANS_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "ATTACH_ROT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_NONE", ATTACH_ROT_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_COMP", ATTACH_ROT_COMP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_UV", ATTACH_ROT_UV );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_RST", ATTACH_ROT_RST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_LMN", ATTACH_ROT_LMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_EtaMN", ATTACH_ROT_EtaMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTACH_ROT_TYPE", "ATTACH_ROT_NUM_TYPES", ATTACH_ROT_NUM_TYPES );
    assert( r >= 0 );

    r = se->RegisterEnum( "ATTRIBUTABLE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_PARM", ATTROBJ_PARM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_GEOM", ATTROBJ_GEOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_VEH", ATTROBJ_VEH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_SUBSURF", ATTROBJ_SUBSURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_MEASURE", ATTROBJ_MEASURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_LINK", ATTROBJ_LINK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_ADVLINK", ATTROBJ_ADVLINK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_ATTR", ATTROBJ_ATTR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_COLLECTION", ATTROBJ_COLLECTION );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTABLE_TYPE", "ATTROBJ_FREE", ATTROBJ_FREE );
    assert( r >= 0 );

    r = se->RegisterEnum( "ATTRIBUTE_EVENT_GROUP" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTE_EVENT_GROUP", "ATTR_GROUP_NONE", ATTR_GROUP_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ATTRIBUTE_EVENT_GROUP", "ATTR_GROUP_WATERMARK", ATTR_GROUP_WATERMARK );
    assert( r >= 0 );


    r = se->RegisterEnum( "AUX_GEOM_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_ROTOR_TIP_PATH", AUX_GEOM_ROTOR_TIP_PATH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_ROTOR_BURST", AUX_GEOM_ROTOR_BURST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_THREE_PT_GROUND", AUX_GEOM_THREE_PT_GROUND );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_TWO_PT_GROUND", AUX_GEOM_TWO_PT_GROUND );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_ONE_PT_GROUND", AUX_GEOM_ONE_PT_GROUND );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_THREE_PT_CCE", AUX_GEOM_THREE_PT_CCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_SUPER_CONE", AUX_GEOM_SUPER_CONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_SINGLE_GEAR", AUX_GEOM_SINGLE_GEAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_TIRE_SPRAY", AUX_GEOM_TIRE_SPRAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_WHEEL_TIRE_FAILURE", AUX_GEOM_WHEEL_TIRE_FAILURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_ROTOR_FRAGMENT", AUX_GEOM_ROTOR_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "AUX_GEOM_THROWN_BLADE", AUX_GEOM_THROWN_BLADE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "AUX_GEOM_MODE", "NUM_AUX_GEOM_MODES", NUM_AUX_GEOM_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "BOGIE_SPACING_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOGIE_SPACING_TYPE", "BOGIE_CENTER_DIST", BOGIE_CENTER_DIST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOGIE_SPACING_TYPE", "BOGIE_CENTER_DIST_FRAC", BOGIE_CENTER_DIST_FRAC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOGIE_SPACING_TYPE", "BOGIE_GAP", BOGIE_GAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOGIE_SPACING_TYPE", "BOGIE_GAP_FRAC", BOGIE_GAP_FRAC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOGIE_SPACING_TYPE", "NUM_BOGIE_SPACING_TYPE", NUM_BOGIE_SPACING_TYPE );
    assert( r >= 0 );


    r = se->RegisterEnum( "BOR_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOR_MODE", "BOR_FLOWTHROUGH", BOR_FLOWTHROUGH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOR_MODE", "BOR_UPPER", BOR_UPPER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOR_MODE", "BOR_LOWER", BOR_LOWER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "BOR_MODE", "BOR_NUM_MODES", BOR_NUM_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "CAMBER_INPUT_FLAG" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMBER_INPUT_FLAG", "MAX_CAMB", MAX_CAMB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMBER_INPUT_FLAG", "DESIGN_CL", DESIGN_CL );
    assert( r >= 0 );


    r = se->RegisterEnum( "CAMERA_VIEW" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_TOP", CAM_TOP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_FRONT", CAM_FRONT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_FRONT_YUP", CAM_FRONT_YUP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_LEFT", CAM_LEFT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_LEFT_ISO", CAM_LEFT_ISO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_BOTTOM", CAM_BOTTOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_REAR", CAM_REAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_RIGHT", CAM_RIGHT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_RIGHT_ISO", CAM_RIGHT_ISO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAMERA_VIEW", "CAM_CENTER", CAM_CENTER );
    assert( r >= 0 );


    r = se->RegisterEnum( "CAP_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "NO_END_CAP", vsp::NO_END_CAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "FLAT_END_CAP", vsp::FLAT_END_CAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "ROUND_END_CAP", vsp::ROUND_END_CAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "EDGE_END_CAP", vsp::EDGE_END_CAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "SHARP_END_CAP", vsp::SHARP_END_CAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "ROUND_EXT_END_CAP_NONE", vsp::ROUND_EXT_END_CAP_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "ROUND_EXT_END_CAP_LE", vsp::ROUND_EXT_END_CAP_LE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "ROUND_EXT_END_CAP_TE", vsp::ROUND_EXT_END_CAP_TE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "ROUND_EXT_END_CAP_BOTH", vsp::ROUND_EXT_END_CAP_BOTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CAP_TYPE", "NUM_END_CAP_OPTIONS", vsp::NUM_END_CAP_OPTIONS );
    assert( r >= 0 );


    r = se->RegisterEnum( "CFD_CONTROL_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_MIN_EDGE_LEN", CFD_MIN_EDGE_LEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_MAX_EDGE_LEN", CFD_MAX_EDGE_LEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_MAX_GAP", CFD_MAX_GAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_NUM_CIRCLE_SEGS", CFD_NUM_CIRCLE_SEGS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_GROWTH_RATIO", CFD_GROWTH_RATIO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_LIMIT_GROWTH_FLAG", CFD_LIMIT_GROWTH_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_INTERSECT_SUBSURFACE_FLAG", CFD_INTERSECT_SUBSURFACE_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_HALF_MESH_FLAG", CFD_HALF_MESH_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_FIELD_FLAG", CFD_FAR_FIELD_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_MAX_EDGE_LEN", CFD_FAR_MAX_EDGE_LEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_MAX_GAP", CFD_FAR_MAX_GAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_NUM_CIRCLE_SEGS", CFD_FAR_NUM_CIRCLE_SEGS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_SIZE_ABS_FLAG", CFD_FAR_SIZE_ABS_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_LENGTH", CFD_FAR_LENGTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_WIDTH", CFD_FAR_WIDTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_HEIGHT", CFD_FAR_HEIGHT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_X_SCALE", CFD_FAR_X_SCALE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_Y_SCALE", CFD_FAR_Y_SCALE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_Z_SCALE", CFD_FAR_Z_SCALE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_LOC_MAN_FLAG", CFD_FAR_LOC_MAN_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_LOC_X", CFD_FAR_LOC_X );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_LOC_Y", CFD_FAR_LOC_Y );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_FAR_LOC_Z", CFD_FAR_LOC_Z );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_SPLIT_JOIN_SURFS_FLAG", CFD_SPLIT_JOIN_SURFS_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_PARALLEL_MESH_FLAG", CFD_PARALLEL_MESH_FLAG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_CONTROL_TYPE", "CFD_POGS_NUM_REFINE", CFD_POGS_NUM_REFINE );
    assert( r >= 0 );


    r = se->RegisterEnum( "CFD_MESH_EXPORT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_STL_FILE_NAME", CFD_STL_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_POLY_FILE_NAME", CFD_POLY_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_TRI_FILE_NAME", CFD_TRI_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_OBJ_FILE_NAME", CFD_OBJ_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_DAT_FILE_NAME", CFD_DAT_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_KEY_FILE_NAME", CFD_KEY_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_GMSH_FILE_NAME", CFD_GMSH_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_TKEY_FILE_NAME", CFD_TKEY_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_FACET_FILE_NAME", CFD_FACET_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_VSPGEOM_FILE_NAME", CFD_VSPGEOM_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_POGS_FILE_NAME", CFD_POGS_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_EXPORT_TYPE", "CFD_NUM_FILE_NAMES", CFD_NUM_FILE_NAMES );
    assert( r >= 0 );


    r = se->RegisterEnum( "CFD_MESH_SOURCE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "POINT_SOURCE", POINT_SOURCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "LINE_SOURCE", LINE_SOURCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "BOX_SOURCE", BOX_SOURCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "ULINE_SOURCE", ULINE_SOURCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "WLINE_SOURCE", WLINE_SOURCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CFD_MESH_SOURCE_TYPE", "NUM_SOURCE_TYPES", NUM_SOURCE_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "CF_LAM_EQN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_LAM_EQN", "CF_LAM_BLASIUS", CF_LAM_BLASIUS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_LAM_EQN", "CF_LAM_BLASIUS_W_HEAT", CF_LAM_BLASIUS_W_HEAT ); // TODO: Remove or implement
    assert( r >= 0 );


    r = se->RegisterEnum( "CF_TURB_EQN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_EXPLICIT_FIT_SPALDING", CF_TURB_EXPLICIT_FIT_SPALDING );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_EXPLICIT_FIT_SPALDING_CHI", CF_TURB_EXPLICIT_FIT_SPALDING_CHI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_EXPLICIT_FIT_SCHOENHERR", CF_TURB_EXPLICIT_FIT_SCHOENHERR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_IMPLICIT_KARMAN", DO_NOT_USE_CF_TURB_IMPLICIT_KARMAN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_IMPLICIT_SCHOENHERR", CF_TURB_IMPLICIT_SCHOENHERR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_IMPLICIT_KARMAN_SCHOENHERR", CF_TURB_IMPLICIT_KARMAN_SCHOENHERR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_POWER_LAW_BLASIUS", CF_TURB_POWER_LAW_BLASIUS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_POWER_LAW_PRANDTL_LOW_RE", CF_TURB_POWER_LAW_PRANDTL_LOW_RE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_POWER_LAW_PRANDTL_MEDIUM_RE", CF_TURB_POWER_LAW_PRANDTL_MEDIUM_RE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_POWER_LAW_PRANDTL_HIGH_RE", CF_TURB_POWER_LAW_PRANDTL_HIGH_RE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_SCHLICHTING_COMPRESSIBLE", CF_TURB_SCHLICHTING_COMPRESSIBLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_SCHLICHTING_INCOMPRESSIBLE", DO_NOT_USE_CF_TURB_SCHLICHTING_INCOMPRESSIBLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_SCHLICHTING_PRANDTL", DO_NOT_USE_CF_TURB_SCHLICHTING_PRANDTL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_SCHULTZ_GRUNOW_HIGH_RE", DO_NOT_USE_CF_TURB_SCHULTZ_GRUNOW_HIGH_RE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_SCHULTZ_GRUNOW_SCHOENHERR", CF_TURB_SCHULTZ_GRUNOW_SCHOENHERR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_WHITE_CHRISTOPH_COMPRESSIBLE", DO_NOT_USE_CF_TURB_WHITE_CHRISTOPH_COMPRESSIBLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_ROUGHNESS_SCHLICHTING_AVG", CF_TURB_ROUGHNESS_SCHLICHTING_AVG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_ROUGHNESS_SCHLICHTING_LOCAL", DO_NOT_USE_CF_TURB_ROUGHNESS_SCHLICHTING_LOCAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_ROUGHNESS_WHITE", DO_NOT_USE_CF_TURB_ROUGHNESS_WHITE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "CF_TURB_ROUGHNESS_SCHLICHTING_AVG_FLOW_CORRECTION", CF_TURB_ROUGHNESS_SCHLICHTING_AVG_FLOW_CORRECTION );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CF_TURB_EQN", "DO_NOT_USE_CF_TURB_HEATTRANSFER_WHITE_CHRISTOPH", DO_NOT_USE_CF_TURB_HEATTRANSFER_WHITE_CHRISTOPH );
    assert( r >= 0 );


    r = se->RegisterEnum( "CHEVRON_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_TYPE", "CHEVRON_NONE", CHEVRON_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_TYPE", "CHEVRON_PARTIAL", CHEVRON_PARTIAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_TYPE", "CHEVRON_FULL", CHEVRON_FULL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_TYPE", "CHEVRON_NUM_TYPES", CHEVRON_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "CHEVRON_W01_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_W01_MODES", "CHEVRON_W01_SE", CHEVRON_W01_SE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_W01_MODES", "CHEVRON_W01_CW", CHEVRON_W01_CW );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CHEVRON_W01_MODES", "CHEVRON_W01_NUM_MODES", CHEVRON_W01_NUM_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "CLONE_DELETE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CLONE_DELETE_TYPE", "CLONE_DELETE_LEAVE_EMPTY", CLONE_DELETE_LEAVE_EMPTY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CLONE_DELETE_TYPE", "CLONE_DELETE_WITH_ORIGINAL", CLONE_DELETE_WITH_ORIGINAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CLONE_DELETE_TYPE", "CLONE_DELETE_REPLACE", CLONE_DELETE_REPLACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CLONE_DELETE_TYPE", "CLONE_DELETE_NUM_TYPES", CLONE_DELETE_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "COLLISION_ERRORS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COLLISION_ERRORS", "COLLISION_OK", COLLISION_OK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COLLISION_ERRORS", "COLLISION_INTERSECT_NO_SOLUTION", COLLISION_INTERSECT_NO_SOLUTION );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COLLISION_ERRORS", "COLLISION_CLEAR_NO_SOLUTION", COLLISION_CLEAR_NO_SOLUTION );
    assert( r >= 0 );


    r = se->RegisterEnum( "COMPUTATION_FILE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "NO_FILE_TYPE", NO_FILE_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "COMP_GEOM_TXT_TYPE", COMP_GEOM_TXT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "COMP_GEOM_CSV_TYPE", COMP_GEOM_CSV_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "SLICE_TXT_TYPE", SLICE_TXT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "MASS_PROP_TXT_TYPE", MASS_PROP_TXT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "DEGEN_GEOM_CSV_TYPE", DEGEN_GEOM_CSV_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "DEGEN_GEOM_M_TYPE", DEGEN_GEOM_M_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_STL_TYPE", CFD_STL_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_POLY_TYPE", CFD_POLY_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_TRI_TYPE", CFD_TRI_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_OBJ_TYPE", CFD_OBJ_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_DAT_TYPE", CFD_DAT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_KEY_TYPE", CFD_KEY_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_GMSH_TYPE", CFD_GMSH_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_TKEY_TYPE", CFD_TKEY_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "PROJ_AREA_CSV_TYPE", PROJ_AREA_CSV_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "WAVE_DRAG_TXT_TYPE", WAVE_DRAG_TXT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "VSPAERO_PANEL_TRI_TYPE", VSPAERO_PANEL_TRI_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "DRAG_BUILD_CSV_TYPE", DRAG_BUILD_CSV_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_FACET_TYPE", CFD_FACET_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_VSPGEOM_TYPE", CFD_VSPGEOM_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "VSPAERO_VSPGEOM_TYPE", VSPAERO_VSPGEOM_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "COMPUTATION_FILE_TYPE", "CFD_POGS_TYPE", CFD_POGS_TYPE );
    assert( r >= 0 );


    r = se->RegisterEnum( "CONFORMAL_TRIM_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CONFORMAL_TRIM_TYPE", "U_TRIM", U_TRIM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CONFORMAL_TRIM_TYPE", "L_TRIM", L_TRIM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CONFORMAL_TRIM_TYPE", "ETA_TRIM", ETA_TRIM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "CONFORMAL_TRIM_TYPE", "NUM_TRIM_TYPES", NUM_TRIM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "DELIM_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DELIM_TYPE", "DELIM_COMMA", DELIM_COMMA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DELIM_TYPE", "DELIM_USCORE", DELIM_USCORE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DELIM_TYPE", "DELIM_SPACE", DELIM_SPACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DELIM_TYPE", "DELIM_NONE", DELIM_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DELIM_TYPE", "DELIM_NUM_TYPES", DELIM_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "DIMENSION_SET" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIMENSION_SET", "SET_3D", SET_3D );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIMENSION_SET", "SET_2D", SET_2D );
    assert( r >= 0 );


    r = se->RegisterEnum( "DIR_INDEX" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIR_INDEX", "X_DIR", X_DIR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIR_INDEX", "Y_DIR", Y_DIR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIR_INDEX", "Z_DIR", Z_DIR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DIR_INDEX", "ALL_DIR", ALL_DIR );
    assert( r >= 0 );


    r = se->RegisterEnum( "DISPLAY_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DISPLAY_TYPE", "DISPLAY_BEZIER", DISPLAY_BEZIER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DISPLAY_TYPE", "DISPLAY_DEGEN_SURF", DISPLAY_DEGEN_SURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DISPLAY_TYPE", "DISPLAY_DEGEN_PLATE", DISPLAY_DEGEN_PLATE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DISPLAY_TYPE", "DISPLAY_DEGEN_CAMBER", DISPLAY_DEGEN_CAMBER );
    assert( r >= 0 );


    r = se->RegisterEnum( "DRAW_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DRAW_TYPE", "GEOM_DRAW_WIRE", GEOM_DRAW_WIRE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DRAW_TYPE", "GEOM_DRAW_HIDDEN", GEOM_DRAW_HIDDEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DRAW_TYPE", "GEOM_DRAW_SHADE", GEOM_DRAW_SHADE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DRAW_TYPE", "GEOM_DRAW_TEXTURE", GEOM_DRAW_TEXTURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "DRAW_TYPE", "GEOM_DRAW_NONE", GEOM_DRAW_NONE );
    assert( r >= 0 );


    r = se->RegisterEnum( "ERROR_CODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_UNKNOWN", vsp::VSP_UNKNOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_OK", vsp::VSP_OK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_PTR", vsp::VSP_INVALID_PTR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_TYPE", vsp::VSP_INVALID_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CANT_FIND_TYPE", vsp::VSP_CANT_FIND_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CANT_FIND_PARM", vsp::VSP_CANT_FIND_PARM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CANT_FIND_NAME", vsp::VSP_CANT_FIND_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_GEOM_ID", vsp::VSP_INVALID_GEOM_ID );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_FILE_DOES_NOT_EXIST", vsp::VSP_FILE_DOES_NOT_EXIST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_FILE_WRITE_FAILURE", vsp::VSP_FILE_WRITE_FAILURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_FILE_READ_FAILURE", vsp::VSP_FILE_READ_FAILURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_WRONG_GEOM_TYPE", vsp::VSP_WRONG_GEOM_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_WRONG_XSEC_TYPE", vsp::VSP_WRONG_XSEC_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_WRONG_FILE_TYPE", vsp::VSP_WRONG_FILE_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INDEX_OUT_RANGE", vsp::VSP_INDEX_OUT_RANGE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_XSEC_ID", vsp::VSP_INVALID_XSEC_ID );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_ID", vsp::VSP_INVALID_ID );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CANT_SET_NOT_EQ_PARM", vsp::VSP_CANT_SET_NOT_EQ_PARM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_AMBIGUOUS_SUBSURF", vsp::VSP_AMBIGUOUS_SUBSURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_VARPRESET_SETNAME", vsp::VSP_INVALID_VARPRESET_SETNAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_VARPRESET_GROUPNAME", vsp::VSP_INVALID_VARPRESET_GROUPNAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CONFORMAL_PARENT_UNSUPPORTED", vsp::VSP_CONFORMAL_PARENT_UNSUPPORTED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_UNEXPECTED_RESET_REMAP_ID", vsp::VSP_UNEXPECTED_RESET_REMAP_ID );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_INPUT_VAL", vsp::VSP_INVALID_INPUT_VAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_CF_EQN", vsp::VSP_INVALID_CF_EQN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_INVALID_DRIVERS", vsp::VSP_INVALID_DRIVERS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_ADV_LINK_BUILD_FAIL", vsp::VSP_ADV_LINK_BUILD_FAIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_DEPRECATED", vsp::VSP_DEPRECATED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_LINK_LOOP_DETECTED", vsp::VSP_LINK_LOOP_DETECTED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_LINK_OUTPUT_NOT_ASSIGNED", vsp::VSP_LINK_OUTPUT_NOT_ASSIGNED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_DUPLICATE_NAME", vsp::VSP_DUPLICATE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_GUI_DEVICE_DEACTIVATED", vsp::VSP_GUI_DEVICE_DEACTIVATED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_COULD_NOT_CREATE_BACKGROUND3D", vsp::VSP_COULD_NOT_CREATE_BACKGROUND3D );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_CLONE_ORIGINAL_LOST", vsp::VSP_CLONE_ORIGINAL_LOST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_GEOM_IN_BOTH_SETS", vsp::VSP_GEOM_IN_BOTH_SETS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ERROR_CODE", "VSP_NUM_ERROR_CODE", vsp::VSP_NUM_ERROR_CODE );
    assert( r >= 0 );


    r = se->RegisterEnum( "EXCRES_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXCRES_TYPE", "EXCRESCENCE_COUNT", EXCRESCENCE_COUNT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXCRES_TYPE", "EXCRESCENCE_CD", EXCRESCENCE_CD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXCRES_TYPE", "EXCRESCENCE_PERCENT_GEOM", EXCRESCENCE_PERCENT_GEOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXCRES_TYPE", "EXCRESCENCE_MARGIN", EXCRESCENCE_MARGIN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXCRES_TYPE", "EXCRESCENCE_DRAGAREA", EXCRESCENCE_DRAGAREA );
    assert( r >= 0 );


    r = se->RegisterEnum( "EXPORT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_FELISA", EXPORT_FELISA ); // TODO: Remove or implement
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_XSEC", EXPORT_XSEC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_STL", EXPORT_STL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_AWAVE", EXPORT_AWAVE ); // TODO: Remove or implement
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_NASCART", EXPORT_NASCART );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_POVRAY", EXPORT_POVRAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_CART3D", EXPORT_CART3D );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_VSPGEOM", EXPORT_VSPGEOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_VORXSEC", EXPORT_VORXSEC ); // TODO: Remove or implement
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_XSECGEOM", EXPORT_XSECGEOM ); // TODO: Remove or implement
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_GMSH", EXPORT_GMSH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_X3D", EXPORT_X3D );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_STEP", EXPORT_STEP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_PLOT3D", EXPORT_PLOT3D );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_IGES", EXPORT_IGES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_BEM", EXPORT_BEM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_DXF", EXPORT_DXF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_FACET", EXPORT_FACET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_SVG", EXPORT_SVG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_PMARC", EXPORT_PMARC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_OBJ", EXPORT_OBJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_SELIG_AIRFOIL", EXPORT_SELIG_AIRFOIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_BEZIER_AIRFOIL", EXPORT_BEZIER_AIRFOIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_IGES_STRUCTURE", EXPORT_IGES_STRUCTURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_STEP_STRUCTURE", EXPORT_STEP_STRUCTURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_STEP_STITCH", EXPORT_STEP_STITCH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "EXPORT_TYPE", "EXPORT_IGES_STITCH", EXPORT_IGES_STITCH );
    assert( r >= 0 );

    r = se->RegisterEnum( "FEA_BC_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_BCM_USER", FEA_BCM_USER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_BCM_ALL", FEA_BCM_ALL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_BCM_PIN", FEA_BCM_PIN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_BCM_SYMM", FEA_BCM_SYMM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_BCM_ASYMM", FEA_BCM_ASYMM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_MODE", "FEA_NUM_BCM_MODES", FEA_NUM_BCM_MODES );
    assert( r >= 0 );

    r = se->RegisterEnum( "FEA_BC_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_TYPE", "FEA_BC_STRUCTURE", FEA_BC_STRUCTURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_TYPE", "FEA_BC_PART", FEA_BC_PART );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_TYPE", "FEA_BC_SUBSURF", FEA_BC_SUBSURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_BC_TYPE", "FEA_NUM_BC_TYPES", FEA_NUM_BC_TYPES );
    assert( r >= 0 );

    r = se->RegisterEnum( "FEA_CROSS_SECT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_GENERAL", FEA_XSEC_GENERAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_CIRC", FEA_XSEC_CIRC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_PIPE", FEA_XSEC_PIPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_I", FEA_XSEC_I );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_RECT", FEA_XSEC_RECT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_CROSS_SECT_TYPE", "FEA_XSEC_BOX", FEA_XSEC_BOX );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_EXPORT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_MASS_FILE_NAME", FEA_MASS_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_NASTRAN_FILE_NAME", FEA_NASTRAN_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_NKEY_FILE_NAME", FEA_NKEY_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_CALCULIX_FILE_NAME", FEA_CALCULIX_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_STL_FILE_NAME", FEA_STL_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_GMSH_FILE_NAME", FEA_GMSH_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_IGES_FILE_NAME", FEA_IGES_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_STEP_FILE_NAME", FEA_STEP_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_EXPORT_TYPE", "FEA_NUM_FILE_NAMES", FEA_NUM_FILE_NAMES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_FIX_PT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_FIX_PT_TYPE", "FEA_FIX_PT_ON_BODY", FEA_FIX_PT_ON_BODY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_FIX_PT_TYPE", "FEA_FIX_PT_GLOBAL_XYZ", FEA_FIX_PT_GLOBAL_XYZ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_FIX_PT_TYPE", "FEA_FIX_PT_DELTA_XYZ", FEA_FIX_PT_DELTA_XYZ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_FIX_PT_TYPE", "FEA_FIX_PT_DELTA_UVN", FEA_FIX_PT_DELTA_UVN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_FIX_PT_TYPE", "FEA_NUM_FIX_PT_TYPES", FEA_NUM_FIX_PT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_MATERIAL_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_MATERIAL_TYPE", "FEA_ISOTROPIC", FEA_ISOTROPIC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_MATERIAL_TYPE", "FEA_ENG_ORTHO", FEA_ENG_ORTHO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_MATERIAL_TYPE", "FEA_ENG_ORTHO_TRANS_ISO", FEA_ENG_ORTHO_TRANS_ISO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_MATERIAL_TYPE", "FEA_LAMINATE", FEA_LAMINATE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_MATERIAL_TYPE", "FEA_NUM_MAT_TYPES", FEA_NUM_MAT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_ORIENTATION_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_GLOBAL_X", FEA_ORIENT_GLOBAL_X );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_GLOBAL_Y", FEA_ORIENT_GLOBAL_Y );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_GLOBAL_Z", FEA_ORIENT_GLOBAL_Z );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_COMP_X", FEA_ORIENT_COMP_X );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_COMP_Y", FEA_ORIENT_COMP_Y );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_COMP_Z", FEA_ORIENT_COMP_Z );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_PART_U", FEA_ORIENT_PART_U );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_PART_V", FEA_ORIENT_PART_V );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_OML_U", FEA_ORIENT_OML_U );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_OML_V", FEA_ORIENT_OML_V );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_OML_R", FEA_ORIENT_OML_R );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_OML_S", FEA_ORIENT_OML_S );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_ORIENT_OML_T", FEA_ORIENT_OML_T );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_ORIENTATION_TYPE", "FEA_NUM_ORIENT_TYPES", FEA_NUM_ORIENT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_PART_ELEMENT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_DEPRECATED", FEA_DEPRECATED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_SHELL", FEA_SHELL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_BEAM", FEA_BEAM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_SHELL_AND_BEAM", FEA_SHELL_AND_BEAM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_NO_ELEMENTS", FEA_NO_ELEMENTS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_ELEMENT_TYPE", "FEA_NUM_ELEMENT_TYPES", FEA_NUM_ELEMENT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_PART_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_SLICE", FEA_SLICE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_RIB", FEA_RIB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_SPAR", FEA_SPAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_FIX_POINT", FEA_FIX_POINT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_DOME", FEA_DOME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_RIB_ARRAY", FEA_RIB_ARRAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_SLICE_ARRAY", FEA_SLICE_ARRAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_TRIM", FEA_TRIM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_SKIN", FEA_SKIN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_POLY_SPAR", FEA_POLY_SPAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_PART_TYPE", "FEA_NUM_TYPES", FEA_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_RIB_NORMAL" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_RIB_NORMAL", "NO_NORMAL", NO_NORMAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_RIB_NORMAL", "LE_NORMAL", LE_NORMAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_RIB_NORMAL", "TE_NORMAL", TE_NORMAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_RIB_NORMAL", "SPAR_NORMAL", SPAR_NORMAL );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_SHELL_TREATMENT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SHELL_TREATMENT_TYPE", "FEA_KEEP", FEA_KEEP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SHELL_TREATMENT_TYPE", "FEA_DELETE", FEA_DELETE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SHELL_TREATMENT_TYPE", "FEA_NUM_SHELL_TREATMENT_TYPES", FEA_NUM_SHELL_TREATMENT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_SLICE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "XY_BODY", XY_BODY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "YZ_BODY", YZ_BODY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "XZ_BODY", XZ_BODY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "XY_ABS", XY_ABS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "YZ_ABS", YZ_ABS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "XZ_ABS", XZ_ABS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_SLICE_TYPE", "SPINE_NORMAL", SPINE_NORMAL );
    assert( r >= 0 );

    r = se->RegisterEnum( "FEA_POLY_SPAR_POINT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_POLY_SPAR_POINT", "POLY_SPAR_POINT_U01", POLY_SPAR_POINT_U01 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_POLY_SPAR_POINT", "POLY_SPAR_POINT_U0N", POLY_SPAR_POINT_U0N );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_POLY_SPAR_POINT", "POLY_SPAR_POINT_ETA", POLY_SPAR_POINT_ETA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_POLY_SPAR_POINT", "NUM_POLY_SPAR_POINT_TYPES", NUM_POLY_SPAR_POINT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FEA_UNIT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_UNIT_TYPE", "SI_UNIT", SI_UNIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_UNIT_TYPE", "CGS_UNIT", CGS_UNIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_UNIT_TYPE", "MPA_UNIT", MPA_UNIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_UNIT_TYPE", "BFT_UNIT", BFT_UNIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FEA_UNIT_TYPE", "BIN_UNIT", BIN_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "FIT_MODEL_TARGET_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FIT_MODEL_TARGET_TYPE", "FIT_MODEL_FIXED", FIT_MODEL_FIXED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FIT_MODEL_TARGET_TYPE", "FIT_MODEL_FREE", FIT_MODEL_FREE );
    assert( r >= 0 );

    r = se->RegisterEnum( "FF_B_EQN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_MANUAL", FF_B_MANUAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_SCHEMENSKY_BODY", FF_B_SCHEMENSKY_FUSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_SCHEMENSKY_NACELLE", FF_B_SCHEMENSKY_NACELLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_HOERNER_STREAMBODY", FF_B_HOERNER_STREAMBODY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_TORENBEEK", FF_B_TORENBEEK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_SHEVELL", FF_B_SHEVELL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_COVERT", FF_B_COVERT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_JENKINSON_FUSE", FF_B_JENKINSON_FUSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_JENKINSON_WING_NACELLE", FF_B_JENKINSON_WING_NACELLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_B_EQN", "FF_B_JENKINSON_AFT_FUSE_NACELLE", FF_B_JENKINSON_AFT_FUSE_NACELLE );
    assert( r >= 0 );


    r = se->RegisterEnum( "FF_W_EQN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_MANUAL", FF_W_MANUAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_EDET_CONV", FF_W_EDET_CONV );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_EDET_ADV", FF_W_EDET_ADV );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_HOERNER", FF_W_HOERNER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_COVERT", FF_W_COVERT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_WILLIAMS", FF_W_SHEVELL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_KROO", FF_W_KROO );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_TORENBEEK", FF_W_TORENBEEK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_DATCOM", FF_W_DATCOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_SCHEMENSKY_6_SERIES_AF", FF_W_SCHEMENSKY_6_SERIES_AF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_SCHEMENSKY_4_SERIES_AF", FF_W_SCHEMENSKY_4_SERIES_AF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_JENKINSON_WING", FF_W_JENKINSON_WING );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_JENKINSON_TAIL", FF_W_JENKINSON_TAIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FF_W_EQN", "FF_W_SCHEMENSKY_SUPERCRITICAL_AF", FF_W_SCHEMENSKY_SUPERCRITICAL_AF );
    assert( r >= 0 );


    r = se->RegisterEnum( "FRAGMENT_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "ONE_THIRD_ROTOR_FRAGMENT", ONE_THIRD_ROTOR_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "INTERMEDIATE_FRAGMENT", INTERMEDIATE_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "ALTERNATE_FRAGMENT", ALTERNATE_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "FAN_FRAGMENT", FAN_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "SMALL_FRAGMENT", SMALL_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "GENERIC_FRAGMENT", GENERIC_FRAGMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FRAGMENT_MODE", "NUM_FRAGMENT_MODES", NUM_FRAGMENT_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FREESTREAM_PD_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FREESTREAM_PD_UNITS", "PD_UNITS_IMPERIAL", PD_UNITS_IMPERIAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FREESTREAM_PD_UNITS", "PD_UNITS_METRIC", PD_UNITS_METRIC );
    assert( r >= 0 );


    r = se->RegisterEnum( "FILE_CHOOSER_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_MODE", "OPEN", OPEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_MODE", "SAVE", SAVE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_MODE", "NUM_FILE_CHOOSER_MODES", NUM_FILE_CHOOSER_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "FILE_CHOOSER_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_TYPE", "FC_OPENVSP", FC_OPENVSP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_TYPE", "FC_NATIVE", FC_NATIVE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "FILE_CHOOSER_TYPE", "NUM_FILE_CHOOSER_TYPES", NUM_FILE_CHOOSER_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "GDEV" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_TAB", GDEV_TAB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SCROLL_TAB", GDEV_SCROLL_TAB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_GROUP", GDEV_GROUP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_PARM_BUTTON", GDEV_PARM_BUTTON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_INPUT", GDEV_INPUT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_OUTPUT", GDEV_OUTPUT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SLIDER", GDEV_SLIDER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SLIDER_ADJ_RANGE", GDEV_SLIDER_ADJ_RANGE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_CHECK_BUTTON", GDEV_CHECK_BUTTON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_RADIO_BUTTON", GDEV_RADIO_BUTTON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_TOGGLE_BUTTON", GDEV_TOGGLE_BUTTON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_TOGGLE_RADIO_GROUP", GDEV_TOGGLE_RADIO_GROUP ); // TODO: Implement or remove
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_TRIGGER_BUTTON", GDEV_TRIGGER_BUTTON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_COUNTER", GDEV_COUNTER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_CHOICE", GDEV_CHOICE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_ADD_CHOICE_ITEM", GDEV_ADD_CHOICE_ITEM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SLIDER_INPUT", GDEV_SLIDER_INPUT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SLIDER_ADJ_RANGE_INPUT", GDEV_SLIDER_ADJ_RANGE_INPUT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SLIDER_ADJ_RANGE_TWO_INPUT", GDEV_SLIDER_ADJ_RANGE_TWO_INPUT ); // TODO: Implement or remove
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_FRACT_PARM_SLIDER", GDEV_FRACT_PARM_SLIDER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_STRING_INPUT", GDEV_STRING_INPUT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_INDEX_SELECTOR", GDEV_INDEX_SELECTOR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_COLOR_PICKER", GDEV_COLOR_PICKER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_YGAP", GDEV_YGAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_DIVIDER_BOX", GDEV_DIVIDER_BOX );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_BEGIN_SAME_LINE", GDEV_BEGIN_SAME_LINE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_END_SAME_LINE", GDEV_END_SAME_LINE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_FORCE_WIDTH", GDEV_FORCE_WIDTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "GDEV_SET_FORMAT", GDEV_SET_FORMAT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "NUM_GDEV_TYPES", NUM_GDEV_TYPES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GDEV", "ALL_GDEV_TYPES", ALL_GDEV_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "GEAR_CONFIGURATION_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "GEAR_CONFIGURATION_DOWN", GEAR_CONFIGURATION_DOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "GEAR_CONFIGURATION_UP", GEAR_CONFIGURATION_UP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "GEAR_CONFIGURATION_UP_AND_DOWN", GEAR_CONFIGURATION_UP_AND_DOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "GEAR_CONFIGURATION_INTERMEDIATE", GEAR_CONFIGURATION_INTERMEDIATE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "GEAR_CONFIGURATION_ALL", GEAR_CONFIGURATION_ALL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_CONFIGURATION_MODES", "NUM_GEAR_CONFIGURATION_MODES", NUM_GEAR_CONFIGURATION_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "GEAR_RETRACT_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_RETRACT_MODES", "GEAR_STOWED_POSITION", GEAR_STOWED_POSITION );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_RETRACT_MODES", "GEAR_MECHANISM", GEAR_MECHANISM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_RETRACT_MODES", "NUM_GEAR_RETRACT_MODES", NUM_GEAR_RETRACT_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "GEAR_SUSPENSION_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_SUSPENSION_MODES", "GEAR_SUSPENSION_NOMINAL", GEAR_SUSPENSION_NOMINAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_SUSPENSION_MODES", "GEAR_SUSPENSION_COMPRESSED", GEAR_SUSPENSION_COMPRESSED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_SUSPENSION_MODES", "GEAR_SUSPENSION_EXTENDED", GEAR_SUSPENSION_EXTENDED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEAR_SUSPENSION_MODES", "NUM_GEAR_SUSPENSION_MODES", NUM_GEAR_SUSPENSION_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "GENDER" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GENDER", "MALE", MALE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GENDER", "FEMALE", FEMALE );
    assert( r >= 0 );


    r = se->RegisterEnum( "GEOMETRY_ANALYSIS_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "EXTERNAL_INTERFERENCE", EXTERNAL_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PACKAGING_INTERFERENCE", PACKAGING_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "EXTERNAL_SELF_INTERFERENCE", EXTERNAL_SELF_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PLANE_STATIC_DISTANCE_INTERFERENCE", PLANE_STATIC_DISTANCE_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PLANE_2PT_ANGLE_INTERFERENCE", PLANE_2PT_ANGLE_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "GEAR_CG_TIPBACK_ANALYSIS", GEAR_CG_TIPBACK_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PLANE_1PT_ANGLE_INTERFERENCE", PLANE_1PT_ANGLE_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "GEAR_WEIGHT_DISTRIBUTION_ANALYSIS", GEAR_WEIGHT_DISTRIBUTION_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "GEAR_TIPOVER_ANALYSIS", GEAR_TIPOVER_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "GEAR_TURN_ANALYSIS", GEAR_TURN_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "VISIBLE_FROM_POINT_ANALYSIS", VISIBLE_FROM_POINT_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "CCE_INTERFERENCE", CCE_INTERFERENCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "LINEAR_SWEPT_VOLUME_ANALYSIS", LINEAR_SWEPT_VOLUME_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "VISIBLE_AT_SURF_ANALYSIS", VISIBLE_AT_SURF_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "RISK_ANGLE", RISK_ANGLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "COMP_GEOM", COMP_GEOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PLANAR_SLICE", PLANAR_SLICE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "PROJ_AREA", PROJ_AREA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "MASS_PROP", MASS_PROP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "AERO_CENTER", AERO_CENTER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GEOMETRY_ANALYSIS_TYPE", "NUM_INTERFERENCE_TYPES", NUM_INTERFERENCE_TYPES );
    assert( r >= 0 );

    r = se->RegisterEnum( "GUI_GEOM_SCREEN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "POD_GEOM_SCREEN", POD_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "FUSELAGE_GEOM_SCREEN", FUSELAGE_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "MS_WING_GEOM_SCREEN", MS_WING_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "BLANK_GEOM_SCREEN", BLANK_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "MESH_GEOM_SCREEN", MESH_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "STACK_GEOM_SCREEN", STACK_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "CUSTOM_GEOM_SCREEN", CUSTOM_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "PT_CLOUD_GEOM_SCREEN", PT_CLOUD_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "PROP_GEOM_SCREEN", PROP_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "HINGE_GEOM_SCREEN", HINGE_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "MULT_GEOM_SCREEN", MULT_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "CONFORMAL_SCREEN", CONFORMAL_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "ELLIPSOID_GEOM_SCREEN", ELLIPSOID_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "BOR_GEOM_SCREEN", BOR_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "WIRE_FRAME_GEOM_SCREEN", WIRE_FRAME_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "HUMAN_GEOM_SCREEN", HUMAN_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "ROUTING_GEOM_SCREEN", ROUTING_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "AUXILIARY_GEOM_SCREEN", AUXILIARY_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "GEAR_GEOM_SCREEN", GEAR_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "COBRA_GEOM_SCREEN", COBRA_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "NGON_MESH_GEOM_SCREEN", NGON_MESH_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "CLONE_GEOM_SCREEN", CLONE_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "NUM_GEOM_SCREENS", NUM_GEOM_SCREENS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_GEOM_SCREEN", "ALL_GEOM_SCREENS", ALL_GEOM_SCREENS );
    assert( r >= 0 );


    r = se->RegisterEnum( "GUI_VSP_SCREEN" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_ADV_LINK_SCREEN", VSP_ADV_LINK_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_ADV_LINK_VAR_RENAME_SCREEN", VSP_ADV_LINK_VAR_RENAME_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_AERO_STRUCT_SCREEN", VSP_AERO_STRUCT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_AIRFOIL_CURVES_EXPORT_SCREEN", VSP_AIRFOIL_CURVES_EXPORT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_AIRFOIL_POINTS_EXPORT_SCREEN", VSP_AIRFOIL_POINTS_EXPORT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_BACKGROUND_SCREEN", VSP_BACKGROUND_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_BACKGROUND3D_SCREEN", VSP_BACKGROUND3D_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_BACKGROUND3D_PREVIEW_SCREEN", VSP_BACKGROUND3D_PREVIEW_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_BEM_OPTIONS_SCREEN", VSP_BEM_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_CFD_MESH_SCREEN", VSP_CFD_MESH_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_CLIPPING_SCREEN", VSP_CLIPPING_SCREEN );
    assert( r >= 0 );

    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_CLONE_NAME_SUFFIX_SCREEN", VSP_CLONE_NAME_SUFFIX_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_COMP_GEOM_SCREEN", VSP_COMP_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_COR_SCREEN", VSP_COR_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_CURVE_EDIT_SCREEN", VSP_CURVE_EDIT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_DEGEN_GEOM_SCREEN", VSP_DEGEN_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_DESIGN_VAR_SCREEN", VSP_DESIGN_VAR_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_DXF_OPTIONS_SCREEN", VSP_DXF_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_EXPORT_SCREEN", VSP_EXPORT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_FEA_PART_EDIT_SCREEN", VSP_FEA_PART_EDIT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_FEA_XSEC_SCREEN", VSP_FEA_XSEC_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_FIT_MODEL_SCREEN", VSP_FIT_MODEL_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_IGES_OPTIONS_SCREEN", VSP_IGES_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_IGES_STRUCTURE_OPTIONS_SCREEN", VSP_IGES_STRUCTURE_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_EXPORT_CUSTOM_SCRIPT", VSP_EXPORT_CUSTOM_SCRIPT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_GEOMETRY_ANALYSIS_SCREEN", VSP_GEOMETRY_ANALYSIS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_IMPORT_SCREEN", VSP_IMPORT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_LIGHTING_SCREEN", VSP_LIGHTING_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MANAGE_GEOM_SCREEN", VSP_MANAGE_GEOM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MANAGE_TEXTURE_SCREEN", VSP_MANAGE_TEXTURE_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MASS_PROP_SCREEN", VSP_MASS_PROP_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MATERIAL_EDIT_SCREEN", VSP_MATERIAL_EDIT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MEASURE_SCREEN", VSP_MEASURE_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MODE_EDITOR_SCREEN", VSP_MODE_EDITOR_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SNAP_TO_SCREEN", VSP_SNAP_TO_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PARASITE_DRAG_SCREEN", VSP_PARASITE_DRAG_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PARM_DEBUG_SCREEN", VSP_PARM_DEBUG_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PARM_LINK_SCREEN", VSP_PARM_LINK_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PARM_SCREEN", VSP_PARM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PICK_SET_SCREEN", VSP_PICK_SET_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PREFERENCES_SCREEN", VSP_PREFERENCES_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PROJECTION_SCREEN", VSP_PROJECTION_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_PSLICE_SCREEN", VSP_PSLICE_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_RESULTS_VIEWER_SCREEN", VSP_RESULTS_VIEWER_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SCREENSHOT_SCREEN", VSP_SCREENSHOT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SELECT_FILE_SCREEN", VSP_SELECT_FILE_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SET_EDITOR_SCREEN", VSP_SET_EDITOR_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_STEP_OPTIONS_SCREEN", VSP_STEP_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_STEP_STRUCTURE_OPTIONS_SCREEN", VSP_STEP_STRUCTURE_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_STL_OPTIONS_SCREEN", VSP_STL_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_STRUCT_SCREEN", VSP_STRUCT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_STRUCT_ASSEMBLY_SCREEN", VSP_STRUCT_ASSEMBLY_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SURFACE_INTERSECTION_SCREEN", VSP_SURFACE_INTERSECTION_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SVG_OPTIONS_SCREEN", VSP_SVG_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_USER_PARM_SCREEN", VSP_USER_PARM_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_ATTRIBUTE_EXPLORER_SCREEN", VSP_ATTRIBUTE_EXPLORER_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VAR_PRESET_SCREEN", VSP_VAR_PRESET_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VEH_NOTES_SCREEN", VSP_VEH_NOTES_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VEH_SCREEN", VSP_VEH_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VIEW_SCREEN", VSP_VIEW_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VSPAERO_PLOT_SCREEN", VSP_VSPAERO_PLOT_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_VSPAERO_SCREEN", VSP_VSPAERO_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_XSEC_SCREEN", VSP_XSEC_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_WAVEDRAG_SCREEN", VSP_WAVEDRAG_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_SPLIT_STITCH_OPTIONS_SCREEN", VSP_SPLIT_STITCH_OPTIONS_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_MAIN_SCREEN", VSP_MAIN_SCREEN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_NUM_SCREENS", VSP_NUM_SCREENS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "GUI_VSP_SCREEN", "VSP_ALL_SCREENS", VSP_ALL_SCREENS );
    assert( r >= 0 );


    r = se->RegisterEnum( "INIT_EDIT_XSEC_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INIT_EDIT_XSEC_TYPE", "EDIT_XSEC_CIRCLE", EDIT_XSEC_CIRCLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INIT_EDIT_XSEC_TYPE", "EDIT_XSEC_ELLIPSE", EDIT_XSEC_ELLIPSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INIT_EDIT_XSEC_TYPE", "EDIT_XSEC_RECTANGLE", EDIT_XSEC_RECTANGLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INIT_EDIT_XSEC_TYPE", "NUM_INIT_EDIT_XSEC_TYPES", NUM_INIT_EDIT_XSEC_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "IMPORT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_STL", IMPORT_STL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_NASCART", IMPORT_NASCART );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_CART3D_TRI", IMPORT_CART3D_TRI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_XSEC_MESH", IMPORT_XSEC_MESH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_PTS", IMPORT_PTS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_V2", IMPORT_V2 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_BEM", IMPORT_BEM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_XSEC_WIRE", IMPORT_XSEC_WIRE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "IMPORT_TYPE", "IMPORT_P3D_WIRE", IMPORT_P3D_WIRE );
    assert( r >= 0 );


    r = se->RegisterEnum( "INTERSECT_EXPORT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INTERSECT_EXPORT_TYPE", "INTERSECT_IGES_FILE_NAME", INTERSECT_IGES_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INTERSECT_EXPORT_TYPE", "INTERSECT_STEP_FILE_NAME", INTERSECT_STEP_FILE_NAME );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "INTERSECT_EXPORT_TYPE", "INTERSECT_NUM_FILE_NAMES", INTERSECT_NUM_FILE_NAMES );
    assert( r >= 0 );


    r = se->RegisterEnum( "LEN_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_MM", LEN_MM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_CM", LEN_CM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_M", LEN_M );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_IN", LEN_IN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_FT", LEN_FT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_YD", LEN_YD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "LEN_UNITLESS", LEN_UNITLESS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "LEN_UNITS", "NUM_LEN_UNIT", NUM_LEN_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "MASS_UNIT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_UNIT_G", MASS_UNIT_G );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_UNIT_KG", MASS_UNIT_KG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_UNIT_TONNE", MASS_UNIT_TONNE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_UNIT_LBM", MASS_UNIT_LBM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_UNIT_SLUG", MASS_UNIT_SLUG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "MASS_LBFSEC2IN", MASS_LBFSEC2IN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MASS_UNIT", "NUM_MASS_UNIT", NUM_MASS_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "MESH_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MESH_TYPE", "TRI_MESH_TYPE", TRI_MESH_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MESH_TYPE", "QUAD_MESH_TYPE", QUAD_MESH_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MESH_TYPE", "NGON_MESH_TYPE", NGON_MESH_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MESH_TYPE", "NUM_MESH_TYPE", NUM_MESH_TYPE );
    assert( r >= 0 );


    r = se->RegisterEnum( "MOTION_EXTENT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_FORWARD_INF", EXTENT_FORWARD_INF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_REVERSE_INF", EXTENT_REVERSE_INF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_FORWARD_FINITE", EXTENT_FORWARD_FINITE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_REVERSE_FINITE", EXTENT_REVERSE_FINITE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_SLIDER_FULL", EXTENT_SLIDER_FULL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_SLIDER_BEFORE", EXTENT_SLIDER_BEFORE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "EXTENT_SLIDER_AFTER", EXTENT_SLIDER_AFTER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "MOTION_EXTENT_TYPE", "MOTION_NUM_EXTENT_TYPES", MOTION_NUM_EXTENT_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "OBJ_ID_LENGTH" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "OBJ_ID_LENGTH", "ID_LENGTH_ATTR", ID_LENGTH_ATTR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "OBJ_ID_LENGTH", "ID_LENGTH_ATTRCOLL", ID_LENGTH_ATTRCOLL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "OBJ_ID_LENGTH", "ID_LENGTH_PARMCONTAINER", ID_LENGTH_PARMCONTAINER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "OBJ_ID_LENGTH", "ID_LENGTH_PARM", ID_LENGTH_PARM );
    assert( r >= 0 );


    r = se->RegisterEnum( "PARM_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_DOUBLE_TYPE", PARM_DOUBLE_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_INT_TYPE",    PARM_INT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_BOOL_TYPE",   PARM_BOOL_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_FRACTION_TYPE", PARM_FRACTION_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_LIMITED_INT_TYPE", PARM_LIMITED_INT_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PARM_TYPE", "PARM_NOTEQ_TYPE", PARM_NOTEQ_TYPE );
    assert( r >= 0 );


    r = se->RegisterEnum( "PATCH_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_NONE", vsp::PATCH_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_POINT", vsp::PATCH_POINT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_LINE", vsp::PATCH_LINE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_COPY", vsp::PATCH_COPY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_HALFWAY", vsp::PATCH_HALFWAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PATCH_TYPE", "PATCH_NUM_TYPES", vsp::PATCH_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "PCURV_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PCURV_TYPE", "LINEAR", LINEAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PCURV_TYPE", "PCHIP", PCHIP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PCURV_TYPE", "CEDIT", CEDIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PCURV_TYPE", "APPROX_CEDIT", APPROX_CEDIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PCURV_TYPE", "NUM_PCURV_TYPE", NUM_PCURV_TYPE );
    assert( r >= 0 );


    r = se->RegisterEnum( "PRES_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_PSF", PRES_UNIT_PSF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_PSI", PRES_UNIT_PSI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_BA", PRES_UNIT_BA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_PA", PRES_UNIT_PA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_KPA", PRES_UNIT_KPA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_MPA", PRES_UNIT_MPA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_INCHHG", PRES_UNIT_INCHHG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_MMHG", PRES_UNIT_MMHG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_MMH20", PRES_UNIT_MMH20 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_MB", PRES_UNIT_MB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "PRES_UNIT_ATM", PRES_UNIT_ATM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PRES_UNITS", "NUM_PRES_UNIT", NUM_PRES_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROJ_BNDY_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_BNDY_TYPE", "NO_BOUNDARY", NO_BOUNDARY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_BNDY_TYPE", "SET_BOUNDARY", SET_BOUNDARY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_BNDY_TYPE", "GEOM_BOUNDARY", GEOM_BOUNDARY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_BNDY_TYPE", "NUM_PROJ_BNDY_OPTIONS", NUM_PROJ_BNDY_OPTIONS );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROJ_DIR_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "X_PROJ", X_PROJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "Y_PROJ", Y_PROJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "Z_PROJ", Z_PROJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "GEOM_PROJ", GEOM_PROJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "VEC_PROJ", VEC_PROJ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_DIR_TYPE", "NUM_PROJ_DIR_OPTIONS", NUM_PROJ_DIR_OPTIONS );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROJ_TGT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "SET_TARGET", SET_TARGET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "GEOM_TARGET", GEOM_TARGET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "MODE_TARGET", MODE_TARGET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "Z_TARGET", Z_TARGET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "XYZ_TARGET", XYZ_TARGET );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROJ_TGT_TYPE", "NUM_PROJ_TGT_OPTIONS", NUM_PROJ_TGT_OPTIONS );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROP_AZIMUTH_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_AZIMUTH_MODE", "PROP_AZI_UNIFORM", PROP_AZI_UNIFORM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_AZIMUTH_MODE", "PROP_AZI_FREE", PROP_AZI_FREE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_AZIMUTH_MODE", "PROP_AZI_BALANCED", PROP_AZI_BALANCED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_AZIMUTH_MODE", "NUM_PROP_AZI", NUM_PROP_AZI );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROP_BLADE_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_BLADE_MODE", "PROP_BLADE_TRADITIONAL", PROP_BLADE_TRADITIONAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_BLADE_MODE", "PROP_BLADE_NONTRADITIONAL", PROP_BLADE_NONTRADITIONAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_BLADE_MODE", "NUM_PROP_BLADE_MODES", NUM_PROP_BLADE_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROP_DRIVERS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "RPM_PROP_DRIVER", RPM_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "CT_PROP_DRIVER", CT_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "CP_PROP_DRIVER", CP_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "T_PROP_DRIVER", T_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "ETA_PROP_DRIVER", ETA_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "J_PROP_DRIVER", J_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "P_PROP_DRIVER", P_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "CQ_PROP_DRIVER", CQ_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "Q_PROP_DRIVER", Q_PROP_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_DRIVERS", "NUM_PROP_DRIVER", NUM_PROP_DRIVER );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROP_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_MODE", "PROP_BLADES", PROP_BLADES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_MODE", "PROP_BOTH", PROP_BOTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_MODE", "PROP_DISK", PROP_DISK );
    assert( r >= 0 );


    r = se->RegisterEnum( "PROP_PCURVE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_CHORD", PROP_CHORD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_TWIST", PROP_TWIST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_RAKE", PROP_RAKE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_SKEW", PROP_SKEW );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_SWEEP", PROP_SWEEP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_THICK", PROP_THICK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_CLI", PROP_CLI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_AXIAL", PROP_AXIAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "PROP_TANGENTIAL", PROP_TANGENTIAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "PROP_PCURVE", "NUM_PROP_PCURVE", NUM_PROP_PCURVE );
    assert( r >= 0 );


    r = se->RegisterEnum( "REORDER_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REORDER_TYPE", "REORDER_MOVE_UP", REORDER_MOVE_UP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REORDER_TYPE", "REORDER_MOVE_DOWN", REORDER_MOVE_DOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REORDER_TYPE", "REORDER_MOVE_TOP", REORDER_MOVE_TOP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REORDER_TYPE", "REORDER_MOVE_BOTTOM", REORDER_MOVE_BOTTOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REORDER_TYPE", "NUM_REORDER_TYPES", NUM_REORDER_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "REF_WING_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REF_WING_TYPE", "MANUAL_REF", MANUAL_REF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REF_WING_TYPE", "COMPONENT_REF", COMPONENT_REF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "REF_WING_TYPE", "NUM_REF_TYPES", NUM_REF_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "RES_DATA_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "INVALID_TYPE", INVALID_TYPE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "BOOL_DATA", BOOL_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "INT_DATA", INT_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "DOUBLE_DATA", DOUBLE_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "STRING_DATA", STRING_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "VEC3D_DATA", VEC3D_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "INT_MATRIX_DATA", INT_MATRIX_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "DOUBLE_MATRIX_DATA", DOUBLE_MATRIX_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "ATTR_COLLECTION_DATA", ATTR_COLLECTION_DATA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_DATA_TYPE", "PARM_REFERENCE_DATA", PARM_REFERENCE_DATA );
    assert( r >= 0 );


    r = se->RegisterEnum( "RES_GEOM_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_GEOM_TYPE", "MESH_INDEXED_TRI", MESH_INDEXED_TRI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_GEOM_TYPE", "MESH_SLICE_TRI", MESH_SLICE_TRI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_GEOM_TYPE", "GEOM_XSECS", GEOM_XSECS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RES_GEOM_TYPE", "MESH_INDEX_AND_SLICE_TRI", MESH_INDEX_AND_SLICE_TRI );
    assert( r >= 0 );


    r = se->RegisterEnum( "RHO_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_SLUG_FT3", RHO_UNIT_SLUG_FT3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_G_CM3", RHO_UNIT_G_CM3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_KG_M3", RHO_UNIT_KG_M3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_TONNE_MM3", RHO_UNIT_TONNE_MM3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_LBM_FT3", RHO_UNIT_LBM_FT3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_LBFSEC2_IN4", RHO_UNIT_LBFSEC2_IN4 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "RHO_UNIT_LBM_IN3", RHO_UNIT_LBM_IN3 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "RHO_UNITS", "NUM_RHO_UNIT", NUM_RHO_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "ROUTE_PT_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_COMP", ROUTE_PT_COMP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_UV", ROUTE_PT_UV );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_RST", ROUTE_PT_RST );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_LMN", ROUTE_PT_LMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_EtaMN", ROUTE_PT_EtaMN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_TYPE", "ROUTE_PT_NUM_TYPES", ROUTE_PT_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "ROUTE_PT_DELTA_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_DELTA_TYPE", "ROUTE_PT_DELTA_XYZ", ROUTE_PT_DELTA_XYZ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_DELTA_TYPE", "ROUTE_PT_DELTA_COMP", ROUTE_PT_DELTA_COMP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_DELTA_TYPE", "ROUTE_PT_DELTA_UVN", ROUTE_PT_DELTA_UVN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "ROUTE_PT_DELTA_TYPE", "ROUTE_PT_DELTA_NUM_TYPES", ROUTE_PT_DELTA_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "SCALE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_WIDTH", SCALE_WIDTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_HEIGHT", SCALE_HEIGHT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_WIDTH_HEIGHT", SCALE_WIDTH_HEIGHT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_RESOLUTION", SCALE_RESOLUTION );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_WIDTH_AR", SCALE_WIDTH_AR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "SCALE_HEIGHT_AR", SCALE_HEIGHT_AR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SCALE_TYPE", "NUM_SCALE_TYPES", NUM_SCALE_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "SET_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "SET_NONE", SET_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "SET_ALL", SET_ALL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "SET_SHOWN", SET_SHOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "SET_NOT_SHOWN", SET_NOT_SHOWN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "SET_FIRST_USER", SET_FIRST_USER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "MIN_NUM_USER", MIN_NUM_USER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SET_TYPE", "MAX_NUM_SETS", MAX_NUM_SETS );
    assert( r >= 0 );


    r = se->RegisterEnum( "STACK_PRESETS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_DEFAULT", STACK_PRESET_DEFAULT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_CYLINDER_ENDPTS", STACK_PRESET_CYLINDER_ENDPTS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_CYLINDER_ENDCAPS", STACK_PRESET_CYLINDER_ENDCAPS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWTHRU_OUTLIP_ORIG", STACK_PRESET_FLOWTHRU_OUTLIP_ORIG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWTHRU_INLIP_ORIG", STACK_PRESET_FLOWTHRU_INLIP_ORIG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWTHRU_MID_ORIG", STACK_PRESET_FLOWTHRU_MID_ORIG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_BOTHFACEFACE", STACK_PRESET_BOTHFACEFACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_BOTHLIPFACE", STACK_PRESET_BOTHLIPFACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_BOTHFACELIP", STACK_PRESET_BOTHFACELIP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_BOTHLIPLIP", STACK_PRESET_BOTHLIPLIP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_OUTFACE", STACK_PRESET_OUTFACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_OUTLIP", STACK_PRESET_OUTLIP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_INFACE", STACK_PRESET_INFACE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_INLIP", STACK_PRESET_INLIP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWPATH_BOTH", STACK_PRESET_FLOWPATH_BOTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWPATH_IN", STACK_PRESET_FLOWPATH_IN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STACK_PRESETS", "STACK_PRESET_FLOWPATH_OUT", STACK_PRESET_FLOWPATH_OUT );
    assert( r >= 0 );


    r = se->RegisterEnum( "STEP_REPRESENTATION" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STEP_REPRESENTATION", "STEP_SHELL", STEP_SHELL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "STEP_REPRESENTATION", "STEP_BREP", STEP_BREP );
    assert( r >= 0 );


    r = se->RegisterEnum( "SUBSURF_INCLUDE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INCLUDE", "SS_INC_TREAT_AS_PARENT", SS_INC_TREAT_AS_PARENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INCLUDE", "SS_INC_SEPARATE_TREATMENT", SS_INC_SEPARATE_TREATMENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INCLUDE", "SS_INC_ZERO_DRAG", SS_INC_ZERO_DRAG );
    assert( r >= 0 );


    r = se->RegisterEnum( "SUBSURF_INOUT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INOUT", "INSIDE", INSIDE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INOUT", "OUTSIDE", OUTSIDE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_INOUT", "NONE", NONE );
    assert( r >= 0 );


    r = se->RegisterEnum( "SUBSURF_LINE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_LINE_TYPE", "CONST_U", CONST_U );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_LINE_TYPE", "CONST_W", CONST_W );
    assert( r >= 0 );


    r = se->RegisterEnum( "SUBSURF_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_LINE", SS_LINE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_RECTANGLE", SS_RECTANGLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_ELLIPSE", SS_ELLIPSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_CONTROL", SS_CONTROL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_LINE_ARRAY", SS_LINE_ARRAY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_FINITE_LINE", SS_FINITE_LINE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_XSEC_CURVE", SS_XSEC_CURVE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_INTERSECT", SS_INTERSECT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SUBSURF_TYPE", "SS_NUM_TYPES", SS_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "SYM_FLAG" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_XY", SYM_XY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_XZ", SYM_XZ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_YZ", SYM_YZ );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_ROT_X", SYM_ROT_X );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_ROT_Y", SYM_ROT_Y );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_ROT_Z", SYM_ROT_Z );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_PLANAR_TYPES", SYM_PLANAR_TYPES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_FLAG", "SYM_NUM_TYPES", SYM_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "SYM_XSEC_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_XSEC_TYPE", "SYM_NONE", SYM_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_XSEC_TYPE", "SYM_RL", SYM_RL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_XSEC_TYPE", "SYM_TB", SYM_TB );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "SYM_XSEC_TYPE", "SYM_ALL", SYM_ALL );


    r = se->RegisterEnum( "TEMP_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TEMP_UNITS", "TEMP_UNIT_K", TEMP_UNIT_K );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TEMP_UNITS", "TEMP_UNIT_C", TEMP_UNIT_C );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TEMP_UNITS", "TEMP_UNIT_F", TEMP_UNIT_F );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TEMP_UNITS", "TEMP_UNIT_R", TEMP_UNIT_R );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TEMP_UNITS", "NUM_TEMP_UNIT", NUM_TEMP_UNIT );
    assert( r >= 0 );


    r = se->RegisterEnum( "TIRE_CLEARANCE_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CLEARANCE_MODES", "TIRE_NOMINAL_CLEARANCE", TIRE_NOMINAL_CLEARANCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CLEARANCE_MODES", "TIRE_GROWTH_CLEARANCE", TIRE_GROWTH_CLEARANCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CLEARANCE_MODES", "TIRE_CLEARANCE", TIRE_CLEARANCE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CLEARANCE_MODES", "NUM_TIRE_CLEARANCE_MODES", NUM_TIRE_CLEARANCE_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "TIRE_CONTACT_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CONTACT_MODES", "TIRE_STATIC_LODED_CONTACT", TIRE_STATIC_LODED_CONTACT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CONTACT_MODES", "TIRE_NOMINAL_CONTACT", TIRE_NOMINAL_CONTACT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CONTACT_MODES", "TIRE_GROWTH_CONTACT", TIRE_GROWTH_CONTACT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CONTACT_MODES", "TIRE_FLAT_CONTACT", TIRE_FLAT_CONTACT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_CONTACT_MODES", "NUM_TIRE_CONTACT_MODES", NUM_TIRE_CONTACT_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "TIRE_DIM_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_DIM_MODES", "TIRE_DIM_IN", TIRE_DIM_IN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_DIM_MODES", "TIRE_DIM_MODEL", TIRE_DIM_MODEL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_DIM_MODES", "TIRE_DIM_FRAC", TIRE_DIM_FRAC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_DIM_MODES", "NUM_TIRE_DIM_MODES", NUM_TIRE_DIM_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "TIRE_MODES" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_TRA", TIRE_TRA );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_FAIR_FLANGE", TIRE_FAIR_FLANGE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_FAIR_WHEEL", TIRE_FAIR_WHEEL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_BALLOON", TIRE_BALLOON );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_BALLOON_WHEEL", TIRE_BALLOON_WHEEL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "TIRE_BALLOON_FAIR_WHEEL", TIRE_BALLOON_FAIR_WHEEL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "TIRE_MODES", "NUM_TIRE_MODES", NUM_TIRE_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "VEL_UNITS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_FT_S", V_UNIT_FT_S );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_M_S", V_UNIT_M_S );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_MPH", V_UNIT_MPH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_KM_HR", V_UNIT_KM_HR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_KEAS", V_UNIT_KEAS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_KTAS", V_UNIT_KTAS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VEL_UNITS", "V_UNIT_MACH", V_UNIT_MACH );
    assert( r >= 0 );


    r = se->RegisterEnum( "VIEW_NUM" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_NUM", "VIEW_1", VIEW_1 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_NUM", "VIEW_2HOR", VIEW_2HOR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_NUM", "VIEW_2VER", VIEW_2VER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_NUM", "VIEW_4", VIEW_4 );
    assert( r >= 0 );


    r = se->RegisterEnum( "VIEW_ROT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_ROT", "ROT_0", ROT_0 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_ROT", "ROT_90", ROT_90 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_ROT", "ROT_180", ROT_180 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_ROT", "ROT_270", ROT_270 );
    assert( r >= 0 );


    r = se->RegisterEnum( "VIEW_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_LEFT", VIEW_LEFT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_RIGHT", VIEW_RIGHT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_TOP", VIEW_TOP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_BOTTOM", VIEW_BOTTOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_FRONT", VIEW_FRONT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_REAR", VIEW_REAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VIEW_TYPE", "VIEW_NONE", VIEW_NONE );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSPAERO_NOISE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_NOISE_TYPE", "NOISE_FLYBY", NOISE_FLYBY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_NOISE_TYPE", "NOISE_FOOTPRINT", NOISE_FOOTPRINT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_NOISE_TYPE", "NOISE_STEADY", NOISE_STEADY );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSPAERO_NOISE_UNIT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_NOISE_UNIT", "NOISE_SI", NOISE_SI );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_NOISE_UNIT", "NOISE_ENGLISH", NOISE_ENGLISH );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSPAERO_PROP_MODE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_PROP_MODE", "VSPAERO_PROP_STATIC", VSPAERO_PROP_STATIC );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_PROP_MODE", "VSPAERO_PROP_UNSTEADY", VSPAERO_PROP_UNSTEADY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_PROP_MODE", "VSPAERO_PROP_PSEUDO_STEADY", VSPAERO_PROP_PSEUDO_STEADY );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_PROP_MODE", "VSPAERO_PROP_NUM_MODES", VSPAERO_PROP_NUM_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSPAERO_STABILITY_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_OFF", STABILITY_OFF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_DEFAULT", STABILITY_DEFAULT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_P_ANALYSIS", STABILITY_P_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_Q_ANALYSIS", STABILITY_Q_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_R_ANALYSIS", STABILITY_R_ANALYSIS );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_PITCH", STABILITY_PITCH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_ADJOINT", STABILITY_ADJOINT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STABILITY_TYPE", "STABILITY_NUM_TYPES", STABILITY_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSPAERO_STALL_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STALL_TYPE", "STALL_OFF", STALL_OFF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSPAERO_STALL_TYPE", "STALL_ON", STALL_ON );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSP_SURF_CFD_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_NORMAL", CFD_NORMAL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_NEGATIVE", CFD_NEGATIVE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_TRANSPARENT", CFD_TRANSPARENT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_STRUCTURE", CFD_STRUCTURE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_STIFFENER", CFD_STIFFENER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_CFD_TYPE", "CFD_NUM_TYPES", CFD_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "VSP_SURF_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_TYPE", "NORMAL_SURF", NORMAL_SURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_TYPE", "WING_SURF", WING_SURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_TYPE", "DISK_SURF", DISK_SURF );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "VSP_SURF_TYPE", "NUM_SURF_TYPES", NUM_SURF_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "WHEEL_TIRE_FAILURE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_1LG", WHEEL_TIRE_1LG );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_1SM", WHEEL_TIRE_1SM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_2", WHEEL_TIRE_2 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_3E", WHEEL_TIRE_3E );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_3R", WHEEL_TIRE_3R );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "WHEEL_TIRE_4", WHEEL_TIRE_4 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WHEEL_TIRE_FAILURE_TYPE", "NUM_WHEEL_TIRE_FAILURE_MODES", NUM_WHEEL_TIRE_FAILURE_MODES );
    assert( r >= 0 );


    r = se->RegisterEnum( "W_HINT" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_RIGHT_0", W_RIGHT_0 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_BOTTOM", W_BOTTOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_LEFT", W_LEFT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_TOP", W_TOP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_RIGHT_1", W_RIGHT_1 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "W_HINT", "W_FREE", W_FREE );
    assert( r >= 0 );


    r = se->RegisterEnum( "WING_BLEND" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_FREE", vsp::BLEND_FREE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_ANGLES", vsp::BLEND_ANGLES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_IN_LE_TRAP", vsp::BLEND_MATCH_IN_LE_TRAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_IN_TE_TRAP", vsp::BLEND_MATCH_IN_TE_TRAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_OUT_LE_TRAP", vsp::BLEND_MATCH_OUT_LE_TRAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_OUT_TE_TRAP", vsp::BLEND_MATCH_OUT_TE_TRAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_IN_ANGLES", vsp::BLEND_MATCH_IN_ANGLES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_MATCH_LE_ANGLES", vsp::BLEND_MATCH_LE_ANGLES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_BLEND", "BLEND_NUM_TYPES", vsp::BLEND_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "WING_DRIVERS" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "AR_WSECT_DRIVER", vsp::AR_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "SPAN_WSECT_DRIVER", vsp::SPAN_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "AREA_WSECT_DRIVER", vsp::AREA_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "TAPER_WSECT_DRIVER", vsp::TAPER_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "AVEC_WSECT_DRIVER", vsp::AVEC_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "ROOTC_WSECT_DRIVER", vsp::ROOTC_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "TIPC_WSECT_DRIVER", vsp::TIPC_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "SECSWEEP_WSECT_DRIVER", vsp::SECSWEEP_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "NUM_WSECT_DRIVER", vsp::NUM_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "SWEEP_WSECT_DRIVER", vsp::SWEEP_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "SWEEPLOC_WSECT_DRIVER", vsp::SWEEPLOC_WSECT_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "WING_DRIVERS", "SECSWEEPLOC_WSECT_DRIVER", vsp::SECSWEEPLOC_WSECT_DRIVER );
    assert( r >= 0 );


    r = se->RegisterEnum( "XDDM_QUANTITY_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XDDM_QUANTITY_TYPE", "XDDM_VAR", XDDM_VAR );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XDDM_QUANTITY_TYPE", "XDDM_CONST", XDDM_CONST );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_CLOSE_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_NONE", CLOSE_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_SKEWLOW", CLOSE_SKEWLOW );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_SKEWUP", CLOSE_SKEWUP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_SKEWBOTH", CLOSE_SKEWBOTH );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_EXTRAP", CLOSE_EXTRAP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CLOSE_TYPE", "CLOSE_NUM_TYPES", CLOSE_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_CRV_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_UNDEFINED", XS_UNDEFINED );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_POINT", XS_POINT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_CIRCLE", XS_CIRCLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_ELLIPSE", XS_ELLIPSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_SUPER_ELLIPSE", XS_SUPER_ELLIPSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_ROUNDED_RECTANGLE", XS_ROUNDED_RECTANGLE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_GENERAL_FUSE", XS_GENERAL_FUSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FILE_FUSE", XS_FILE_FUSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FOUR_SERIES", XS_FOUR_SERIES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_SIX_SERIES", XS_SIX_SERIES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_BICONVEX", XS_BICONVEX );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_WEDGE", XS_WEDGE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_EDIT_CURVE", XS_EDIT_CURVE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FILE_AIRFOIL", XS_FILE_AIRFOIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_CST_AIRFOIL", XS_CST_AIRFOIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_VKT_AIRFOIL", XS_VKT_AIRFOIL );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FOUR_DIGIT_MOD", XS_FOUR_DIGIT_MOD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FIVE_DIGIT", XS_FIVE_DIGIT );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_FIVE_DIGIT_MOD", XS_FIVE_DIGIT_MOD );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_ONE_SIX_SERIES", XS_ONE_SIX_SERIES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_AC25_773", XS_AC25_773 );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_CRV_TYPE", "XS_NUM_TYPES", XS_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_DRIVERS" );  // TODO: improve these comments
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "WIDTH_XSEC_DRIVER", WIDTH_XSEC_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "AREA_XSEC_DRIVER", AREA_XSEC_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "HEIGHT_XSEC_DRIVER", HEIGHT_XSEC_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "HWRATIO_XSEC_DRIVER", HWRATIO_XSEC_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "NUM_XSEC_DRIVER", NUM_XSEC_DRIVER );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_DRIVERS", "CIRCLE_NUM_XSEC_DRIVER", CIRCLE_NUM_XSEC_DRIVER );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_FLAP_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_FLAP_TYPE", "FLAP_NONE", FLAP_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_FLAP_TYPE", "FLAP_PLAIN", FLAP_PLAIN );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_FLAP_TYPE", "FLAP_NUM_TYPES", FLAP_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_SIDES_TYPE" );  // TODO: improve these comments
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_SIDES_TYPE", "XSEC_BOTH_SIDES", XSEC_BOTH_SIDES );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_SIDES_TYPE", "XSEC_LEFT_SIDE", XSEC_LEFT_SIDE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_SIDES_TYPE", "XSEC_RIGHT_SIDE", XSEC_RIGHT_SIDE );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_TRIM_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TRIM_TYPE", "TRIM_NONE", TRIM_NONE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TRIM_TYPE", "TRIM_X", TRIM_X );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TRIM_TYPE", "TRIM_THICK", TRIM_THICK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TRIM_TYPE", "TRIM_NUM_TYPES", TRIM_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_TYPE" );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_FUSE", XSEC_FUSE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_STACK", XSEC_STACK );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_WING", XSEC_WING );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_CUSTOM", XSEC_CUSTOM );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_PROP", XSEC_PROP );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_TYPE", "XSEC_NUM_TYPES", XSEC_NUM_TYPES );
    assert( r >= 0 );


    r = se->RegisterEnum( "XSEC_WIDTH_SHIFT" ); // TODO: improve these comments
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_WIDTH_SHIFT", "XS_SHIFT_LE", XS_SHIFT_LE );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_WIDTH_SHIFT", "XS_SHIFT_MID", XS_SHIFT_MID );
    assert( r >= 0 );
    r = se->RegisterEnumValue( "XSEC_WIDTH_SHIFT", "XS_SHIFT_TE", XS_SHIFT_TE );
    assert( r >= 0 );
    //
}

//==== Vec3d Constructors ====//
static void Vec3dDefaultConstructor( vec3d *self )
{
    new( self ) vec3d();
}
static void Vec3dCopyConstructor( const vec3d &other, vec3d *self )
{
    new( self ) vec3d( other );
}
static void Vec3dInitConstructor( double x, double y, double z, vec3d *self )
{
    new( self ) vec3d( x, y, z );
}

// Taking the address of the free operator== is ambiguous against the ones the
// standard library declares, so wrap it.
static bool Vec3dEquals( const vec3d &a, const vec3d &b )
{
    return a == b;
}

//==== Register Vec3d Object ====//
void ScriptMgrSingleton::RegisterVec3d( asIScriptEngine* se )
{

    //==== Register vec3d Object =====//
    // The trait flags must describe vec3d exactly as the C++ compiler sees it -- they tell AngelScript
    // which native calling convention to use.  vec3d is trivially copyable (it relies on the implicit
    // copy constructor, copy assignment operator, and destructor), so ARM64 hands it back in the
    // floating point registers d0-d2 rather than through a hidden return pointer.  AngelScript only
    // knows to expect that when asOBJ_APP_CLASS_ALLFLOATS (all members are floating point) and
    // asOBJ_APP_CLASS_ALIGN8 (those members are double, not float) are both set.  Without them every
    // script level 'vec3d a = f();' picks up garbage.  asGetTypeTraits supplies the remaining flags so
    // they track any future change to vec3d's copy semantics automatically.
    int r = se->RegisterObjectType( "vec3d", sizeof( vec3d ), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits< vec3d >() | asOBJ_APP_CLASS_ALLFLOATS | asOBJ_APP_CLASS_ALIGN8 );
    assert( r >= 0 );

    //==== Register the vec3d Constructors  ====//
    r = se->RegisterObjectBehaviour( "vec3d", asBEHAVE_CONSTRUCT, "void f()", VSP_OBJ_LAST( Vec3dDefaultConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "vec3d", asBEHAVE_CONSTRUCT, "void f(double, double, double)", VSP_OBJ_LAST( Vec3dInitConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "vec3d", asBEHAVE_CONSTRUCT, "void f(const vec3d &in)", VSP_OBJ_LAST( Vec3dCopyConstructor ) );
    assert( r >= 0 );

    //==== Register the vec3d Methods  ====//
    r = se->RegisterObjectMethod( "vec3d", "double& opIndex(int) const", VSP_MFN_PR( vec3d, operator[], ( int ), double& ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "double x() const", VSP_MFN( vec3d, x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "double y() const", VSP_MFN( vec3d, y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "double z() const", VSP_MFN( vec3d, z ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d& set_xyz(double x, double y, double z)", VSP_MFN( vec3d, set_xyz ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d& set_x(double x)", VSP_MFN( vec3d, set_x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d& set_y(double y)", VSP_MFN( vec3d, set_y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d& set_z(double z)", VSP_MFN( vec3d, set_z ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void rotate_x(double theta)", VSP_MFN( vec3d, rotate_x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void rotate_y(double theta)", VSP_MFN( vec3d, rotate_y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void rotate_z(double theta)", VSP_MFN( vec3d, rotate_z ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void scale_x(double scale)", VSP_MFN( vec3d, scale_x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void scale_y(double scale)", VSP_MFN( vec3d, scale_y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void scale_z(double scale)", VSP_MFN( vec3d, scale_z ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void offset_x(double offset)", VSP_MFN( vec3d, offset_x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void offset_y(double offset)", VSP_MFN( vec3d, offset_y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "void offset_z(double offset)", VSP_MFN( vec3d, offset_z ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d reflect_xy()", VSP_MFN( vec3d, reflect_xy ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d reflect_xz()", VSP_MFN( vec3d, reflect_xz ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d reflect_yz()", VSP_MFN( vec3d, reflect_yz ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d opAdd(const vec3d &in) const", VSP_OBJ_FIRST_PR( operator+, ( const vec3d&, const vec3d& ), vec3d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d opSub(const vec3d &in) const", VSP_OBJ_FIRST_PR( operator-, ( const vec3d&, const vec3d& ), vec3d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec3d", "vec3d opMul(double b) const", VSP_OBJ_FIRST_PR( operator*, ( const vec3d & a, double b ), vec3d ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "vec3d", "vec3d opMul_r(const vec3d &in) const", VSP_OBJ_FIRST_PR( operator*, ( const vec3d&, const vec3d& ), vec3d ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "vec3d", "vec3d opDiv(double b) const", VSP_OBJ_FIRST_PR( operator/, ( const vec3d&, double b ), vec3d ) );
    assert( r >= 0 );


    // vec3d has had == and != in C++ all along but neither was exposed, so
    // scripts could not compare two vec3d, and array<vec3d> could not be
    // compared either since the array add-on needs the subtype to provide this.
    r = se->RegisterObjectMethod( "vec3d", "bool opEquals(const vec3d &in) const", VSP_OBJ_FIRST( Vec3dEquals ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "vec3d", "double mag() const", VSP_MFN( vec3d, mag ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "vec3d", "void normalize()", VSP_MFN( vec3d, normalize ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double dist(const vec3d& in a, const vec3d& in b)", VSP_FN_PR( dist, ( const vec3d&, const vec3d& ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double dist_squared(const vec3d& in a, const vec3d& in b)", VSP_FN_PR( dist_squared, ( const vec3d&, const vec3d& ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double dot(const vec3d& in a, const vec3d& in b)", VSP_FN_PR( dot, ( const vec3d&, const vec3d& ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d cross(const vec3d& in a, const vec3d& in b)", VSP_FN_PR( cross, ( const vec3d&, const vec3d& ), vec3d  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double angle(const vec3d& in a, const vec3d& in b)", VSP_FN_PR( angle, ( const vec3d&, const vec3d& ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double signed_angle(const vec3d& in a, const vec3d& in b, const vec3d& in ref )", VSP_FN_PR( signed_angle, ( const vec3d & a, const vec3d & b, const vec3d & ref ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double cos_angle(const vec3d& in a, const vec3d& in b )", VSP_FN_PR( cos_angle, ( const vec3d & a, const vec3d & b ), double  ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d RotateArbAxis(const vec3d& in p, double theta, const vec3d& in axis )", VSP_FN_PR( RotateArbAxis, ( const vec3d & p, double theta, const vec3d & axis ), vec3d ) );
    assert( r >= 0 );


}

//==== vec2d Constructors ====//
static void Vec2dDefaultConstructor( vec2d *self )
{
    new( self ) vec2d();
}
static void Vec2dCopyConstructor( const vec2d &other, vec2d *self )
{
    new( self ) vec2d( other );
}
static void Vec2dInitConstructor( double x, double y, vec2d *self )
{
    new( self ) vec2d( x, y );
}

// vec2d has no operator== in C++, and comparing component by component is what a script means by
// it.  The array add-on also needs the subtype to provide opEquals before array<vec2d> can be
// compared, which is why this is registered rather than left out.
static bool Vec2dEquals( const vec2d &a, const vec2d &b )
{
    return a.x() == b.x() && a.y() == b.y();
}

//==== Register Vec2d Object ====//
void ScriptMgrSingleton::RegisterVec2d( asIScriptEngine* se )
{
    // Same trait flags as vec3d, and for the same reason: vec2d is trivially copyable and all of
    // its members are double, so ARM64 returns it in the floating point registers.  AngelScript
    // only knows to expect that when ALLFLOATS and ALIGN8 are both set.
    int r = se->RegisterObjectType( "vec2d", sizeof( vec2d ), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits< vec2d >() | asOBJ_APP_CLASS_ALLFLOATS | asOBJ_APP_CLASS_ALIGN8 );
    assert( r >= 0 );

    //==== Register the vec2d Constructors  ====//
    r = se->RegisterObjectBehaviour( "vec2d", asBEHAVE_CONSTRUCT, "void f()", VSP_OBJ_LAST( Vec2dDefaultConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "vec2d", asBEHAVE_CONSTRUCT, "void f(double, double)", VSP_OBJ_LAST( Vec2dInitConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "vec2d", asBEHAVE_CONSTRUCT, "void f(const vec2d &in)", VSP_OBJ_LAST( Vec2dCopyConstructor ) );
    assert( r >= 0 );

    //==== Register the vec2d Methods  ====//
    r = se->RegisterObjectMethod( "vec2d", "double& opIndex(int) const", VSP_MFN_PR( vec2d, operator[], ( int ), double& ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "double x() const", VSP_MFN( vec2d, x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "double y() const", VSP_MFN( vec2d, y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d& set_xy(double x, double y)", VSP_MFN( vec2d, set_xy ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d& set_x(double x)", VSP_MFN( vec2d, set_x ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d& set_y(double y)", VSP_MFN( vec2d, set_y ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "double mag() const", VSP_MFN( vec2d, mag ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "void normalize()", VSP_MFN( vec2d, normalize ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d opAdd(const vec2d &in) const", VSP_OBJ_FIRST_PR( operator+, ( const vec2d&, const vec2d& ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d opSub(const vec2d &in) const", VSP_OBJ_FIRST_PR( operator-, ( const vec2d&, const vec2d& ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d opMul(double b) const", VSP_OBJ_FIRST_PR( operator*, ( const vec2d & a, double b ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d opMul_r(const vec2d &in) const", VSP_OBJ_FIRST_PR( operator*, ( const vec2d&, const vec2d& ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "vec2d opDiv(double b) const", VSP_OBJ_FIRST_PR( operator/, ( const vec2d&, double b ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "vec2d", "bool opEquals(const vec2d &in) const", VSP_OBJ_FIRST( Vec2dEquals ) );
    assert( r >= 0 );

    //==== Register the vec2d free functions ====//
    r = se->RegisterGlobalFunction( "double dist(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( dist, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double dist_squared(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( dist_squared, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double dot(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( dot, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    // In two dimensions the cross product is a single scalar, not a vector.
    r = se->RegisterGlobalFunction( "double cross(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( cross, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double angle(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( angle, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double cos_angle(const vec2d& in a, const vec2d& in b)", VSP_FN_PR( cos_angle, ( const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec2d proj_pnt_on_line_seg(const vec2d& in line_A, const vec2d& in line_B, const vec2d& in pnt)", VSP_FN_PR( proj_pnt_on_line_seg, ( const vec2d&, const vec2d&, const vec2d& ), vec2d ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double proj_pnt_on_line_u(const vec2d& in line_A, const vec2d& in line_B, const vec2d& in pnt)", VSP_FN_PR( proj_pnt_on_line_u, ( const vec2d&, const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double det(const vec2d& in p0, const vec2d& in p1, const vec2d& in offset)", VSP_FN_PR( det, ( const vec2d&, const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double orient2d(const vec2d& in p0, const vec2d& in p1, const vec2d& in p)", VSP_FN_PR( orient2d, ( const vec2d&, const vec2d&, const vec2d& ), double ) );
    assert( r >= 0 );

    // AngelScript passes these back through &out parameters, so unlike the Python bindings they
    // need no typemap of their own.
    r = se->RegisterGlobalFunction( "int seg_seg_intersect(const vec2d& in pnt_A, const vec2d& in pnt_B, const vec2d& in pnt_C, const vec2d& in pnt_D, vec2d& out int_pnt, double& out t1, double& out t2)", VSP_FN_PR( seg_seg_intersect, ( const vec2d&, const vec2d&, const vec2d&, const vec2d&, vec2d&, double&, double& ), int ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void bi_lin_interp(const vec2d& in p0, const vec2d& in p1, const vec2d& in p2, const vec2d& in p3, double s, double t, vec2d& out p_out)", VSP_FN_PR( bi_lin_interp, ( const vec2d&, const vec2d&, const vec2d&, const vec2d&, double, double, vec2d& ), void ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int inverse_bi_lin_interp(const vec2d& in p0, const vec2d& in p1, const vec2d& in p2, const vec2d& in p3, const vec2d& in p, double& out s, double& out t, double& out s2, double& out t2)", VSP_FN_PR( inverse_bi_lin_interp, ( const vec2d&, const vec2d&, const vec2d&, const vec2d&, const vec2d&, double&, double&, double&, double& ), int ) );
    assert( r >= 0 );

    // The polygon functions take a vector<vec2d>, so they go through a thin wrapper that turns the
    // script array into one.  PolyArea2d and PolyCentroid2d are named apart from the vec3d
    // poly_area and poly_centroid on the C++ side only; the script sees the usual names.
    r = se->RegisterGlobalFunction( "bool PointInPolygon(const vec2d& in R, array<vec2d>@+ pnts)", VSP_MFN_GLOBAL( ScriptMgrSingleton, PointInPolygon ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double poly_area(array<vec2d>@+ pnt_vec)", VSP_MFN_GLOBAL( ScriptMgrSingleton, PolyArea2d ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec2d poly_centroid(array<vec2d>@+ pnt_vec)", VSP_MFN_GLOBAL( ScriptMgrSingleton, PolyCentroid2d ), &ScriptMgr );
    assert( r >= 0 );
}

//==== Matrix4d Constructors ====//
static void Matrix4dDefaultConstructor( Matrix4d *self )
{
    new( self ) Matrix4d();
}

//==== Register Matrix4d Object ====//
void ScriptMgrSingleton::RegisterMatrix4d( asIScriptEngine* se )
{
    //==== Register Matrix4d Object =====//


    // Matrix4d relies on the implicit copy constructor, copy assignment operator
    // and destructor, so asOBJ_APP_CLASS_CA claimed an assignment operator that
    // does not exist.  That flag happens to sit outside COMPLEX_MASK on every
    // platform OpenVSP targets, so nothing was broken by it, but letting
    // asGetTypeTraits report the flags keeps them honest if the copy semantics
    // ever change.  Unlike vec3d this needs no ALLFLOATS or ALIGN8: at sixteen
    // members Matrix4d is far past the four member limit for a homogeneous
    // floating point aggregate, so it is always returned through memory.
    int r = se->RegisterObjectType( "Matrix4d", sizeof( Matrix4d ), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits< Matrix4d >() );
    assert( r >= 0 );

    //===== Register the Matrix4d constructor =====//
    r = se->RegisterObjectBehaviour( "Matrix4d", asBEHAVE_CONSTRUCT, "void f()", VSP_OBJ_LAST( Matrix4dDefaultConstructor ) );
    assert( r >= 0 ); // TODO?

    //===== Register the Matrix4d methods =====//


    r = se->RegisterObjectMethod( "Matrix4d", "void loadIdentity()", VSP_MFN( Matrix4d, loadIdentity ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void translatef( const double & in x, const double & in y, const double & in z)", VSP_MFN( Matrix4d, translatef ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void rotateX( const double & in ang )", VSP_MFN( Matrix4d, rotateX ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void rotateY( const double & in ang )", VSP_MFN( Matrix4d, rotateY ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void rotateZ( const double & in ang )", VSP_MFN( Matrix4d, rotateZ ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void rotate( const double & in ang, const vec3d & in axis )", VSP_MFN( Matrix4d, rotate ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void scale( const double & in scale )", VSP_MFN( Matrix4d, scale ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "vec3d xform( const vec3d & in v )", VSP_MFN( Matrix4d, xform ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "vec3d getAngles()", VSP_MFN( Matrix4d, getAngles ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void loadXZRef()", VSP_MFN( Matrix4d, loadXZRef ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void loadXYRef()", VSP_MFN( Matrix4d, loadXYRef ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void loadYZRef()", VSP_MFN( Matrix4d, loadYZRef ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "Matrix4d", "void affineInverse()", VSP_MFN( Matrix4d, affineInverse ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod("Matrix4d", "void buildXForm( const vec3d & in pos, const vec3d & in rot, const vec3d & in cent_rot )", VSP_MFN(Matrix4d, buildXForm) );
    assert( r >= 0 ); // TODO: Example

    //TODO: Expose additional functions to the API (i.e. matMult)

}

//==== Register Custom Geom Mgr Object ====//
void ScriptMgrSingleton::RegisterCustomGeomMgr( asIScriptEngine* se )
{

    int r;
    r = se->RegisterGlobalFunction( "string AddParm( int type, const string & in name, const string & in group )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AddParm ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "string GetCurrCustomGeom()",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, GetCurrCustomGeom ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "string GetCustomParm( int index )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, GetCustomParm ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction( "int AddGui( int type, const string & in label = string(), const string & in parm_name = string(), const string & in group_name = string(), double range = 10.0 )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AddGui ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void UpdateGui( int gui_id, const string & in parm_id )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AddUpdateGui ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "string AddXSecSurf()",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AddXSecSurf ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void RemoveXSecSurf(const string & in xsec_id)",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, RemoveXSecSurf ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void ClearXSecSurfs()",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, ClearXSecSurfs ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SkinXSecSurf( bool closed_flag = false )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SkinXSecSurf ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void CloneSurf(int index, Matrix4d & in mat)",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, CloneSurf ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void TransformSurf(int index, Matrix4d & in mat)",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, TransformSurf ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction( "void SetVspSurfType( int type, int surf_index = -1 )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetVspSurfType ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SetVspSurfCfdType( int type, int surf_index = -1 )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetVspSurfCfdType ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SetCustomXSecLoc( const string & in xsec_id, const vec3d & in loc )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetCustomXSecLoc ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "vec3d GetCustomXSecLoc( const string & in xsec_id )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, GetCustomXSecLoc ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SetCustomXSecRot( const string & in xsec_id, const vec3d & in rot )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetCustomXSecRot ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "vec3d GetCustomXSecRot( const string & in xsec_id )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, GetCustomXSecRot ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "bool CheckClearTriggerEvent( int gui_id )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, CheckClearTriggerEvent ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction(
        "void SetupCustomDefaultSource( int type, int surf_index, double l1, double r1, double u1, double w1, double l2 = 0, double r2 = 0, double u2 = 0, double w2 = 0 )",
        VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetupCustomDefaultSource ), &CustomGeomMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ClearAllCustomDefaultSources()",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, ClearAllCustomDefaultSources ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SetCustomCenter( double x, double y, double z )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, SetCustomCenter ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "string AppendXSec( const string & in xsec_surf_id, int type )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AppendCustomXSec ), &CustomGeomMgr );
    assert( r );

    // WARNING: Both versions of the AppendCustomXSec must be available to avoid breaking existing CustomGeom scripts


    r = se->RegisterGlobalFunction( "string AppendCustomXSec( const string & in xsec_surf_id, int type )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, AppendCustomXSec ), &CustomGeomMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void CutCustomXSec( const string & in xsec_surf_id, int index )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, CutCustomXSec ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction( "void CopyCustomXSec( const string & in xsec_surf_id, int index )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, CopyCustomXSec ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction( "void PasteCustomXSec( const string & in xsec_surf_id, int index )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, PasteCustomXSec ), &CustomGeomMgr );
    assert( r ); // TODO: Example


    r = se->RegisterGlobalFunction( "string InsertCustomXSec( const string & in xsec_surf_id, int type, int index )",
                                    VSP_MFN_GLOBAL( CustomGeomMgrSingleton, InsertCustomXSec ), &CustomGeomMgr );
    assert( r ); // TODO: Example


}



//==== Register Adv Link Mgr Object ====//
void ScriptMgrSingleton::RegisterAdvLinkMgr( asIScriptEngine* se )
{
    int r;

    r = se->RegisterGlobalFunction( "array<string>@+ GetAdvLinkNames()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAdvLinkNames ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetLinkIndex( const string & in name )", VSP_FN( vsp::GetLinkIndex ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAdvLinkName( int index )", VSP_FN( vsp::GetAdvLinkName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetAdvLinkName( int index, const string & in name )", VSP_FN( vsp::SetAdvLinkName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAdvLink( int index )", VSP_FN( vsp::DelAdvLink ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelAllAdvLinks()", VSP_FN( vsp::DelAllAdvLinks ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CopyAirfoil( const string & in geom_id, int index )", VSP_FN( vsp::CopyAirfoil ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void CopyAirfoilByID( const string & in xsec_id )", VSP_FN( vsp::CopyAirfoilByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void PasteAirfoil( const string & in geom_id, int index )", VSP_FN( vsp::PasteAirfoil ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void PasteAirfoilByID( const string & in xsec_id )", VSP_FN( vsp::PasteAirfoilByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ClearSkinning( const string & in geom_id, int index = -1 )", VSP_FN( vsp::ClearSkinning ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ClearSkinningByID( const string & in xsec_id )", VSP_FN( vsp::ClearSkinningByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ResetGeomScale( const string & in geom_id )", VSP_FN( vsp::ResetGeomScale ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AcceptGeomScale( const string & in geom_id )", VSP_FN( vsp::AcceptGeomScale ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetPtCloudPnts( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetPtCloudPnts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string CreatePtCloudGeom( const string & in geom_id )", VSP_FN( vsp::CreatePtCloudGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string CreateNGonMeshGeom( const string & in geom_id )", VSP_FN( vsp::CreateNGonMeshGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string CreateConvexHull( const string & in geom_id )", VSP_FN( vsp::CreateConvexHull ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowSet( int set_index )", VSP_FN( vsp::ShowSet ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void NoShowSet( int set_index )", VSP_FN( vsp::NoShowSet ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowOnlySet( int set_index )", VSP_FN( vsp::ShowOnlySet ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddParmLink( const string & in parm_a_id, const string & in parm_b_id )", VSP_FN( vsp::AddParmLink ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumParmLinks()", VSP_FN( vsp::GetNumParmLinks ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetParmLinkID( int index )", VSP_FN( vsp::GetParmLinkID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllParmLinks()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllParmLinks ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetParmLinkAParm( const string & in link_id )", VSP_FN( vsp::GetParmLinkAParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetParmLinkBParm( const string & in link_id )", VSP_FN( vsp::GetParmLinkBParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkAParm( const string & in link_id, const string & in parm_id )", VSP_FN( vsp::SetParmLinkAParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkBParm( const string & in link_id, const string & in parm_id )", VSP_FN( vsp::SetParmLinkBParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelParmLink( const string & in link_id )", VSP_FN( vsp::DelParmLink ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllParmLinks()", VSP_FN( vsp::DelAllParmLinks ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool LinkAllComp( const string & in parm_a_id, const string & in parm_b_id )", VSP_FN( vsp::LinkAllComp ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool LinkAllGroup( const string & in parm_a_id, const string & in parm_b_id )", VSP_FN( vsp::LinkAllGroup ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool GetParmLinkOffsetFlag( const string & in link_id )", VSP_FN( vsp::GetParmLinkOffsetFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkOffsetFlag( const string & in link_id, bool flag )", VSP_FN( vsp::SetParmLinkOffsetFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool GetParmLinkScaleFlag( const string & in link_id )", VSP_FN( vsp::GetParmLinkScaleFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkScaleFlag( const string & in link_id, bool flag )", VSP_FN( vsp::SetParmLinkScaleFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool GetParmLinkLowerLimitFlag( const string & in link_id )", VSP_FN( vsp::GetParmLinkLowerLimitFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkLowerLimitFlag( const string & in link_id, bool flag )", VSP_FN( vsp::SetParmLinkLowerLimitFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool GetParmLinkUpperLimitFlag( const string & in link_id )", VSP_FN( vsp::GetParmLinkUpperLimitFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParmLinkUpperLimitFlag( const string & in link_id, bool flag )", VSP_FN( vsp::SetParmLinkUpperLimitFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddAdvLink( const string & in name )", VSP_FN( vsp::AddAdvLink ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddAdvLinkInput( int index, const string & in parm_id, const string & in var_name )", VSP_FN( vsp::AddAdvLinkInput ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddAdvLinkOutput( int index, const string & in parm_id, const string & in var_name )", VSP_FN( vsp::AddAdvLinkOutput ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelAllAdvLinkInputs( int index )", VSP_FN( vsp::DelAllAdvLinkInputs ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllAdvLinkOutputs( int index )", VSP_FN( vsp::DelAllAdvLinkOutputs ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAdvLinkInput( int index, const string & in var_name )", VSP_FN( vsp::DelAdvLinkInput ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelAdvLinkOutput( int index, const string & in var_name )", VSP_FN( vsp::DelAdvLinkOutput ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAdvLinkInputNames( int index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAdvLinkInputNames ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAdvLinkInputParms( int index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAdvLinkInputParms ), &ScriptMgr);
    assert( r >= 0 );



    r = se->RegisterGlobalFunction( "array<string>@+ GetAdvLinkOutputNames( int index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAdvLinkOutputNames ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAdvLinkOutputParms( int index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAdvLinkOutputParms ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool ValidateAdvLinkParms( int index )", VSP_FN( vsp::ValidateAdvLinkParms ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAdvLinkCode( int index, const string & in code )", VSP_FN( vsp::SetAdvLinkCode ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAdvLinkCode( int index )", VSP_FN( vsp::GetAdvLinkCode ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SearchReplaceAdvLinkCode( int index, const string & in from, const string & in to )", VSP_FN( vsp::SearchReplaceAdvLinkCode ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool BuildAdvLinkScript( int index )", VSP_FN( vsp::BuildAdvLinkScript ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void WriteAdvLinkCodeFile( int index, const string & in file_name )", VSP_FN( vsp::WriteAdvLinkCodeFile ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderAdvLinkInput( int index, const string & in var_name, int reorder_type )", VSP_FN( vsp::ReorderAdvLinkInput ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderAdvLinkOutput( int index, const string & in var_name, int reorder_type )", VSP_FN( vsp::ReorderAdvLinkOutput ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderGeom( const string & in geom_id, int reorder_type )", VSP_FN( vsp::ReorderGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AttachGeomTexture( const string & in geom_id, const string & in file_name )", VSP_FN( vsp::AttachGeomTexture ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RemoveGeomTexture( const string & in geom_id, const string & in texture_id )", VSP_FN( vsp::RemoveGeomTexture ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomTextureIDVec( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomTextureIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeomTextureFileName( const string & in geom_id, const string & in texture_id )", VSP_FN( vsp::GetGeomTextureFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderSubSurf( const string & in geom_id, const string & in sub_id, int reorder_type )", VSP_FN( vsp::ReorderSubSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderFeaPart( const string & in geom_id, int fea_struct_ind, const string & in part_id, int reorder_type )", VSP_FN( vsp::ReorderFeaPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderFeaPartByID( const string & in part_id, int reorder_type )", VSP_FN( vsp::ReorderFeaPartByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReorderFeaLayer( const string & in material_id, const string & in layer_id, int reorder_type )", VSP_FN( vsp::ReorderFeaLayer ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void IndividualizeFeaPart( const string & in geom_id, int fea_struct_ind, const string & in part_id )", VSP_FN( vsp::IndividualizeFeaPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void IndividualizeFeaPartByID( const string & in part_id )", VSP_FN( vsp::IndividualizeFeaPartByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ProjectPtCloudPts( const string & in geom_id, const string & in target_geom_id, int surf_index, int dir_index )", VSP_FN( vsp::ProjectPtCloudPts ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortParmLinksByA()", VSP_FN( vsp::SortParmLinksByA ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortParmLinksByB()", VSP_FN( vsp::SortParmLinksByB ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ResetPropellerThickness( const string & in geom_id )", VSP_FN( vsp::ResetPropellerThickness ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReadAdvLinkCodeFile( int index, const string & in file_name )", VSP_FN( vsp::ReadAdvLinkCodeFile ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortAdvLinkInputsVar( int index )", VSP_FN( vsp::SortAdvLinkInputsVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortAdvLinkInputsCGP( int index )", VSP_FN( vsp::SortAdvLinkInputsCGP ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortAdvLinkOutputsVar( int index )", VSP_FN( vsp::SortAdvLinkOutputsVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortAdvLinkOutputsCGP( int index )", VSP_FN( vsp::SortAdvLinkOutputsCGP ) );
    assert( r >= 0 );

//
// These methods appear to have been mistakenly exposed to the API.
//
//
//    r = se->RegisterGlobalFunction( "void AddInput( const string & in parm_id, const string & in var_name )",
//                                    VSP_MFN_GLOBAL( AdvLinkMgrSingleton, AddInput ), &AdvLinkMgr );
//    assert( r );
//
//
//    r = se->RegisterGlobalFunction( "void AddOutput( const string & in parm_id, const string & in var_name )",
//                                    VSP_MFN_GLOBAL( AdvLinkMgrSingleton, AddOutput ), &AdvLinkMgr );
//    assert( r );


    r = se->RegisterGlobalFunction( "void SetVar( const string & in var_name, double val )", VSP_MFN_GLOBAL( AdvLinkMgrSingleton, SetVar ), &AdvLinkMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "double GetVar( const string & in var_name )", VSP_MFN_GLOBAL( AdvLinkMgrSingleton, GetVar ), &AdvLinkMgr );
    assert( r );
}

//==== ErrorObj Constructors and Destructor ====//
static void ErrorObjDefaultConstructor( vsp::ErrorObj *self )
{
    new( self ) vsp::ErrorObj();
}
static void ErrorObjCopyConstructor( const vsp::ErrorObj &other, vsp::ErrorObj *self )
{
    new( self ) vsp::ErrorObj( other );
}
static void ErrorObjDestructor( vsp::ErrorObj *self )
{
    self->~ErrorObj();
}
static vsp::ErrorObj & ErrorObjAssign( const vsp::ErrorObj &other, vsp::ErrorObj *self )
{
    *self = other;
    return *self;
}

//==== Register API E Functions ====//
void ScriptMgrSingleton::RegisterAPIErrorObj( asIScriptEngine* se )
{

    //==== Register ErrorObj Object =====//
    // ErrorObj holds a std::string, so it is neither POD nor trivially copyable.
    // It was previously registered asOBJ_POD with no construct or destruct
    // behaviour, which left 'ErrorObj e;' reading uninitialized memory as a
    // std::string and leaked the string of every ErrorObj a script touched,
    // because the destructor was never run.  asGetTypeTraits reports the real
    // constructor, destructor, assignment and copy constructor flags, and the
    // behaviours below give AngelScript something to call for each of them.
    int r = se->RegisterObjectType( "ErrorObj", sizeof( vsp::ErrorObj ), asOBJ_VALUE | asGetTypeTraits< vsp::ErrorObj >() );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "ErrorObj", asBEHAVE_CONSTRUCT, "void f()", VSP_OBJ_LAST( ErrorObjDefaultConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "ErrorObj", asBEHAVE_CONSTRUCT, "void f(const ErrorObj &in)", VSP_OBJ_LAST( ErrorObjCopyConstructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectBehaviour( "ErrorObj", asBEHAVE_DESTRUCT, "void f()", VSP_OBJ_LAST( ErrorObjDestructor ) );
    assert( r >= 0 );

    r = se->RegisterObjectMethod( "ErrorObj", "ErrorObj &opAssign(const ErrorObj &in)", VSP_OBJ_LAST( ErrorObjAssign ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "ErrorObj", "ERROR_CODE GetErrorCode()", VSP_MFN( vsp::ErrorObj, GetErrorCode ) );
    assert( r >= 0 );


    r = se->RegisterObjectMethod( "ErrorObj", "string GetErrorString()", VSP_MFN( vsp::ErrorObj, GetErrorString ) );
    assert( r >= 0 );



}


//==== Register VSP API Functions ====//
void ScriptMgrSingleton::RegisterAPI( asIScriptEngine* se )
{
    int r;

    //==== API Error Functions ====//
    // TODO: Move to ErrorObj group

    r = se->RegisterGlobalFunction( "bool GetErrorLastCallFlag()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, GetErrorLastCallFlag ), &vsp::ErrorMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "int GetNumTotalErrors()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, GetNumTotalErrors ), &vsp::ErrorMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "ErrorObj PopLastError()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, PopLastError ), &vsp::ErrorMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "ErrorObj GetLastError()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, GetLastError ), &vsp::ErrorMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void SilenceErrors()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, SilenceErrors ), &vsp::ErrorMgr );
    assert( r );


    r = se->RegisterGlobalFunction( "void PrintOnErrors()", VSP_MFN_GLOBAL( vsp::ErrorMgrSingleton, PrintOnErrors ), &vsp::ErrorMgr );
    assert( r );

    //==== Visualization Functions ====//

    r = se->RegisterGlobalFunction( "void InitGUI()", VSP_FN( vsp::InitGUI ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void StartGUI()", VSP_FN( vsp::StartGUI ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void EnableStopGUIMenuItem()", VSP_FN( vsp::EnableStopGUIMenuItem ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DisableStopGUIMenuItem()", VSP_FN( vsp::DisableStopGUIMenuItem ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void StopGUI()", VSP_FN( vsp::StopGUI ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PopupMsg( const string & in msg )", VSP_FN( vsp::PopupMsg ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void UpdateGUI()", VSP_FN( vsp::UpdateGUI ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool IsGUIBuild()", VSP_FN( vsp::IsGUIBuild ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void Lock()", VSP_FN( vsp::Lock ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void Unlock()", VSP_FN( vsp::Unlock ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool IsEventLoopRunning()", VSP_FN( vsp::IsEventLoopRunning ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ScreenGrab( const string & in file_name, int w, int h, bool transparentBG, bool autocrop = false )", VSP_FN( vsp::ScreenGrab ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetViewAxis( bool vaxis )", VSP_FN( vsp::SetViewAxis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetShowBorders( bool brdr )", VSP_FN( vsp::SetShowBorders ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction("int GetGeomDrawType( const string & in geom_id )", VSP_FN(vsp::GetGeomDrawType));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction("void SetGeomDrawType( const string & in geom_id, int type )", VSP_FN(vsp::SetGeomDrawType));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction("vec3d GetGeomWireColor( const string & in geom_id )", VSP_FN(vsp::GetGeomWireColor));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction("void SetGeomWireColor( const string & in geom_id, int r, int g, int b )", VSP_FN(vsp::SetGeomWireColor));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction("int GetGeomDisplayType( const string & in geom_id )", VSP_FN(vsp::GetGeomDisplayType));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction("void SetGeomDisplayType( const string & in geom_id, int type )", VSP_FN(vsp::SetGeomDisplayType));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGeomMaterialName( const string & in geom_id )", VSP_FN( vsp::GetGeomMaterialName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeomMaterialName( const string & in geom_id, const string & in name )", VSP_FN( vsp::SetGeomMaterialName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddMaterial( const string & in name, const vec3d & in ambient, const vec3d & in diffuse, const vec3d & in specular, const vec3d & in emissive, const double & in alpha, const double & in shininess )", VSP_FN( vsp::AddMaterial ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetMaterialNames()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetMaterialNames ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetBackground( double r, double g, double b )", VSP_FN( vsp::SetBackground ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAllViews( int view )", VSP_FN( vsp::SetAllViews ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetView( int viewport, int view )", VSP_FN( vsp::SetView ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void FitAllViews()", VSP_FN( vsp::FitAllViews ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ResetViews()", VSP_FN( vsp::ResetViews ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetWindowLayout( int r, int c )", VSP_FN( vsp::SetWindowLayout ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGUIElementDisable( int e, bool state )", VSP_FN( vsp::SetGUIElementDisable ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGUIScreenDisable( int s, bool state )", VSP_FN( vsp::SetGUIScreenDisable ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGeomScreenDisable( int s, bool state )", VSP_FN( vsp::SetGeomScreenDisable ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void HideScreen( int s )", VSP_FN( vsp::HideScreen ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ShowScreen( int s )", VSP_FN( vsp::ShowScreen ) );
    assert( r >= 0 );

    //==== Vehicle Functions ====//

    r = se->RegisterGlobalFunction( "int GetAndResetUpdateCount()", VSP_FN( vsp::GetAndResetUpdateCount ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void Update( bool update_managers = true)", VSP_FN( vsp::Update ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void VSPExit( int error_code )", VSP_FN( vsp::VSPExit ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void VSPCrash( int crash_type )", VSP_FN( vsp::VSPCrash ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ClearVSPModel()", VSP_FN( vsp::ClearVSPModel ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPFileName()", VSP_FN( vsp::GetVSPFileName ) );
    assert( r >= 0 );


    //==== File I/O Functions ====//

    r = se->RegisterGlobalFunction( "void ReadVSPFile( const string & in file_name )", VSP_FN( vsp::ReadVSPFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteVSPFile( const string & in file_name, int set )", VSP_FN( vsp::WriteVSPFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetVSP3FileName( const string & in file_name )", VSP_FN( vsp::SetVSP3FileName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void InsertVSPFile( const string & in file_name, const string & in parent )", VSP_FN( vsp::InsertVSPFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "string ExportFile( const string & in file_name, int thick_set, int file_type, int subsFlag = 1, int thin_set = -1, bool useMode = false, const string & in modeID = \"\" )", VSP_FN( vsp::ExportFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string ImportFile( const string & in file_name, int file_type, const string & in parent )", VSP_FN( vsp::ImportFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "string GetBEMPropID()", VSP_FN( vsp::GetBEMPropID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBEMPropID( const string & in prop_id )", VSP_FN( vsp::SetBEMPropID ) );
    assert( r >= 0 );

    //==== Design File Functions ====//

    r = se->RegisterGlobalFunction( "void ReadApplyDESFile( const string & in file_name )", VSP_FN( vsp::ReadApplyDESFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void WriteDESFile( const string & in file_name )", VSP_FN( vsp::WriteDESFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void ReadApplyXDDMFile( const string & in file_name )", VSP_FN( vsp::ReadApplyXDDMFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void WriteXDDMFile( const string & in file_name )", VSP_FN( vsp::WriteXDDMFile ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "int GetNumDesignVars()", VSP_FN( vsp::GetNumDesignVars ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void AddDesignVar( const string & in parm_id, int type )", VSP_FN( vsp::AddDesignVar ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void AddDesignVarLimits( const string & in parm_id, int type, double lowerlimit, double upperlimit )", VSP_FN( vsp::AddDesignVarLimits ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void DeleteAllDesignVars()", VSP_FN( vsp::DeleteAllDesignVars ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "string GetDesignVar( int index )", VSP_FN( vsp::GetDesignVar ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "int GetDesignVarType( int index )", VSP_FN( vsp::GetDesignVarType ) );
    assert( r >= 0 ); // TODO: Example

    r = se->RegisterGlobalFunction( "int GetDesignVarTypeByID( const string & in parm_id )", VSP_FN( vsp::GetDesignVarTypeByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteDesignVar( const string & in parm_id )", VSP_FN( vsp::DeleteDesignVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetDesignVarVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDesignVarVec ), &ScriptMgr );
    assert( r >= 0 );

    //==== Computations ====//

    r = se->RegisterGlobalFunction( "string ComputeMassProps( int set, int num_slices, int idir = X_DIR )", VSP_FN( vsp::ComputeMassProps ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string ComputeCompGeom( int set, bool half_mesh, int file_export_types )", VSP_FN( vsp::ComputeCompGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string ComputePlaneSlice( int set, int num_slices, const vec3d & in norm, bool auto_bnd, double start_bnd = 0, double end_bnd = 0, bool measureduct = false )", VSP_FN( vsp::ComputePlaneSlice ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ComputeDegenGeom( int set, int file_type )", VSP_FN( vsp::ComputeDegenGeom ) );
    assert( r >= 0 );

    //==== CFD Mesh ====//

    r = se->RegisterGlobalFunction( "string GetComputationFileName( int file_type )", VSP_FN( vsp::GetComputationFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetComputationFileName( int file_type, const string & in file_name )", VSP_FN( vsp::SetComputationFileName ) );
    assert( r >= 0 ); // TODO: FIXME for FEA Mesh


    r = se->RegisterGlobalFunction( "void ComputeCFDMesh( int set, int degenset, int file_type )", VSP_FN( vsp::ComputeCFDMesh ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDMeshModeID( const string & in mode_id )", VSP_FN( vsp::SetCFDMeshModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCFDMeshModeID()", VSP_FN( vsp::GetCFDMeshModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetIntersectModeID( const string & in mode_id )", VSP_FN( vsp::SetIntersectModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetIntersectModeID()", VSP_FN( vsp::GetIntersectModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetSTEPStructureExportID( const string & in struct_id )", VSP_FN( vsp::SetSTEPStructureExportID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetSTEPStructureExportID()", VSP_FN( vsp::GetSTEPStructureExportID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetIGESStructureExportID( const string & in struct_id )", VSP_FN( vsp::SetIGESStructureExportID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetIGESStructureExportID()", VSP_FN( vsp::GetIGESStructureExportID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetAeroStructTargetID( const string & in id )", VSP_FN( vsp::SetAeroStructTargetID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetAeroStructTargetID()", VSP_FN( vsp::GetAeroStructTargetID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetCFDMeshVal( int type )", VSP_FN( vsp::GetCFDMeshVal ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDMeshVal( int type, double val )", VSP_FN( vsp::SetCFDMeshVal ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool GetCFDWakeFlag( const string & in geom_id )", VSP_FN( vsp::GetCFDWakeFlag ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDWakeFlag( const string & in geom_id, bool flag )", VSP_FN( vsp::SetCFDWakeFlag ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumCFDSources( const string & in geom_id )", VSP_FN( vsp::GetNumCFDSources ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCFDSourceName( const string & in geom_id, int source_index )", VSP_FN( vsp::GetCFDSourceName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCFDSourceNameByID( const string & in source_id )", VSP_FN( vsp::GetCFDSourceNameByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDSourceName( const string & in geom_id, int source_index, const string & in name )", VSP_FN( vsp::SetCFDSourceName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDSourceNameByID( const string & in source_id, const string & in name )", VSP_FN( vsp::SetCFDSourceNameByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetCFDSourceType( const string & in geom_id, int source_index )", VSP_FN( vsp::GetCFDSourceType ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetCFDSourceTypeByID( const string & in source_id )", VSP_FN( vsp::GetCFDSourceTypeByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteCFDSource( const string & in geom_id, int source_index )", VSP_FN( vsp::DeleteCFDSource ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteCFDSourceByID( const string & in source_id )", VSP_FN( vsp::DeleteCFDSourceByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetCFDFarFieldGeomID( const string & in geom_id )", VSP_FN( vsp::SetCFDFarFieldGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCFDFarFieldGeomID()", VSP_FN( vsp::GetCFDFarFieldGeomID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllCFDSources()", VSP_FN( vsp::DeleteAllCFDSources ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCFDSourceID( const string & in geom_id, int source_index )", VSP_FN( vsp::GetCFDSourceID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetCFDSourceIDVec( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetCFDSourceIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AdjustAllCFDSourceLen( double mult )", VSP_FN( vsp::AdjustAllCFDSourceLen ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AdjustAllCFDSourceRad( double mult )", VSP_FN( vsp::AdjustAllCFDSourceRad ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddDefaultSources()", VSP_FN( vsp::AddDefaultSources ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction(
        "string AddCFDSource( int type, const string & in geom_id, int surf_index, double l1, double r1, double u1, double w1, double l2 = 0, double r2 = 0, double u2 = 0, double w2 = 0 )",
        VSP_FN( vsp::AddCFDSource ) );
    assert( r >= 0 );


    //==== Analysis Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumAnalysis( )", VSP_FN( vsp::GetNumAnalysis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ ListAnalysis()", VSP_MFN_GLOBAL( ScriptMgrSingleton, ListAnalysis ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAnalysisInputNames(const string & in analysis )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAnalysisInputNames ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAnalysisDoc( const string & in analysis )", VSP_FN( vsp::GetAnalysisDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAnalysisInputDoc( const string & in analysis, const string & in name )", VSP_FN( vsp::GetAnalysisInputDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string ExecAnalysis( const string & in analysis )", VSP_FN( vsp::ExecAnalysis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumAnalysisInputData( const string & in analysis, const string & in name )", VSP_FN( vsp::GetNumAnalysisInputData ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetAnalysisInputType( const string & in analysis, const string & in name )", VSP_FN( vsp::GetAnalysisInputType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<int>@+ GetIntAnalysisInput( const string & in analysis, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetIntAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetDoubleAnalysisInput( const string & in analysis, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDoubleAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetStringAnalysisInput( const string & in analysis, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetStringAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetVec3dAnalysisInput( const string & in analysis, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVec3dAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PrintAnalysisInputs( const string & in analysis )", VSP_FN( vsp::PrintAnalysisInputs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PrintAnalysisDocs( const string & in analysis )", VSP_FN( vsp::PrintAnalysisDocs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAnalysisInputDefaults( const string & in analysis )", VSP_FN( vsp::SetAnalysisInputDefaults ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetIntAnalysisInput( const string & in analysis, const string & in name, array<int>@+ indata_arr, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetIntAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetDoubleAnalysisInput( const string & in analysis, const string & in name, array<double>@+ indata_arr, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetDoubleAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetStringAnalysisInput( const string & in analysis, const string & in name, array<string>@+ indata_arr, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetStringAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetVec3dAnalysisInput( const string & in analysis, const string & in name, array<vec3d>@+ indata_arr, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetVec3dAnalysisInput ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddGeometryAnalysis( )", VSP_FN( vsp::AddGeometryAnalysis ) );
    assert( r >= 0 );
    r = se->RegisterGlobalFunction( "string AddGeometryAnalysisAzEl( const string & in geom_analysis_id, double azimuth, double elevation )", VSP_FN( vsp::AddGeometryAnalysisAzEl ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteGeometryAnalysisAzEl( const string & in geom_analysis_id, int index )", VSP_FN( vsp::DeleteGeometryAnalysisAzEl ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteGeometryAnalysisAzElByID( const string & in geom_analysis_id, const string & in azimuth_parm_id )", VSP_FN( vsp::DeleteGeometryAnalysisAzElByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllGeometryAnalysisAzEl( const string & in geom_analysis_id )", VSP_FN( vsp::DeleteAllGeometryAnalysisAzEl ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumGeometryAnalysisAzEl( const string & in geom_analysis_id )", VSP_FN( vsp::GetNumGeometryAnalysisAzEl ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisAzimuthParm( const string & in geom_analysis_id, int index )", VSP_FN( vsp::GetGeometryAnalysisAzimuthParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisElevationParm( const string & in geom_analysis_id, int index )", VSP_FN( vsp::GetGeometryAnalysisElevationParm ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetGeometryAnalysisAzimuthParmVec( const string & in geom_analysis_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeometryAnalysisAzimuthParmVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetGeometryAnalysisElevationParmVec( const string & in geom_analysis_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeometryAnalysisElevationParmVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeometryAnalysisPrimaryModeID( const string & in geom_analysis_id, const string & in mode_id )", VSP_FN( vsp::SetGeometryAnalysisPrimaryModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisPrimaryModeID( const string & in geom_analysis_id )", VSP_FN( vsp::GetGeometryAnalysisPrimaryModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeometryAnalysisPrimaryGeomID( const string & in geom_analysis_id, const string & in geom_id )", VSP_FN( vsp::SetGeometryAnalysisPrimaryGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisPrimaryGeomID( const string & in geom_analysis_id )", VSP_FN( vsp::GetGeometryAnalysisPrimaryGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeometryAnalysisSecondaryGeomID( const string & in geom_analysis_id, const string & in geom_id )", VSP_FN( vsp::SetGeometryAnalysisSecondaryGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisSecondaryGeomID( const string & in geom_analysis_id )", VSP_FN( vsp::GetGeometryAnalysisSecondaryGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeometryAnalysisDirectionGeomID( const string & in geom_analysis_id, const string & in geom_id )", VSP_FN( vsp::SetGeometryAnalysisDirectionGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeometryAnalysisDirectionGeomID( const string & in geom_analysis_id )", VSP_FN( vsp::GetGeometryAnalysisDirectionGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddGeometryAnalysisCutout( const string & in geom_analysis_id, const string & in ss_id )", VSP_FN( vsp::AddGeometryAnalysisCutout ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteGeometryAnalysisCutout( const string & in geom_analysis_id, const string & in ss_id )", VSP_FN( vsp::DeleteGeometryAnalysisCutout ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetGeometryAnalysisCutoutVec( const string & in geom_analysis_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeometryAnalysisCutoutVec ), &ScriptMgr );
    assert( r >= 0 );



    r = se->RegisterGlobalFunction( "void DeleteGeometryAnalysis( const string & in id )", VSP_FN( vsp::DeleteGeometryAnalysis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllGeometryAnalyses()", VSP_FN( vsp::DeleteAllGeometryAnalyses ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetActiveGeometryAnalysis( const string & in id )", VSP_FN( vsp::SetActiveGeometryAnalysis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetActiveGeometryAnalysis()", VSP_FN( vsp::GetActiveGeometryAnalysis ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string MakeMeshGeom( const string & in ga_id )", VSP_FN( vsp::MakeMeshGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string CreateAndAddBogie( const string & in gear_id )", VSP_FN( vsp::CreateAndAddBogie ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumBogies( const string & in gear_id )", VSP_FN( vsp::GetNumBogies ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllBogies( const string & in gear_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllBogies ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelBogie( const string & in gear_id, const string & in bogie_id )", VSP_FN( vsp::DelBogie ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelAllBogies( const string & in gear_id )", VSP_FN( vsp::DelAllBogies ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBogieStowParentID( const string & in bogie_id, const string & in geom_id )", VSP_FN( vsp::SetBogieStowParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetBogieStowParentID( const string & in bogie_id )", VSP_FN( vsp::GetBogieStowParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBogieMechParentID( const string & in bogie_id, const string & in geom_id )", VSP_FN( vsp::SetBogieMechParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetBogieMechParentID( const string & in bogie_id )", VSP_FN( vsp::GetBogieMechParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetAuxiliaryGeomContactPtID( const string & in geom_id, int index, const string & in bogie_id )", VSP_FN( vsp::SetAuxiliaryGeomContactPtID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetAuxiliaryGeomContactPtID( const string & in geom_id, int index )", VSP_FN( vsp::GetAuxiliaryGeomContactPtID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ReadAuxiliaryGeomCCEFile( const string & in geom_id, const string & in file_name )", VSP_FN( vsp::ReadAuxiliaryGeomCCEFile ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void InitStackPreset( const string & in geom_id, int preset )", VSP_FN( vsp::InitStackPreset ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllGeometryAnalysesIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllGeometryAnalysesIDVec ), &ScriptMgr );
    assert( r >= 0 );

    //==== Results Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumResults( const string & in name )", VSP_FN( vsp::GetNumResults ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetResultsName( const string & in results_id )", VSP_FN( vsp::GetResultsName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetResultsSetDoc( const string & in results_id )",
                                    VSP_FN( vsp::GetResultsSetDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction(
            "string GetResultsEntryDoc( const string & in results_id, const string & in data_name )",
            VSP_FN( vsp::GetResultsEntryDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindResultsID( const string & in name, int index = 0 )", VSP_FN( vsp::FindResultsID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ FindResultsIDs( const string & in name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindResultsIDs ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllResultsIDs()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllResultsIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindLatestResultsID( const string & in name )", VSP_FN( vsp::FindLatestResultsID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumData( const string & in results_id, const string & in data_name )", VSP_FN( vsp::GetNumData ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetResultsType( const string & in results_id, const string & in data_name )", VSP_FN( vsp::GetResultsType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string SummarizeAttributes()", VSP_FN( vsp::SummarizeAttributes ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string SummarizeAttributesAsTree()", VSP_FN( vsp::SummarizeAttributesAsTree ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindAllAttributes()", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAllAttributes ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindAttributesByName( const string & in search_str )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAttributesByName ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindAttributeByName( const string & in search_str, int index )", VSP_FN( vsp::FindAttributeByName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindAttributeInCollection( const string & in coll_id, const string & in search_str, int index )", VSP_FN( vsp::FindAttributeInCollection ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindAttributeNamesInCollection( const string & in collID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAttributeNamesInCollection ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindAttributesInCollection( const string & in collID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAttributesInCollection ), &ScriptMgr);
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ FindAttributesInCollectionByName( const string & in obj_id, const string & in search_str )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAttributesInCollectionByName ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindAttributedObjects()", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindAttributedObjects ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetObjectType( const string & in id )", VSP_FN( vsp::GetObjectType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetObjectTypeName( const string & in id )", VSP_FN( vsp::GetObjectTypeName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetObjectName( const string & in attachID )", VSP_FN( vsp::GetObjectName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetObjectParent( const string & in id )", VSP_FN( vsp::GetObjectParent ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetChildCollection( const string & in attachID )", VSP_FN( vsp::GetChildCollection ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGeomSetCollection( const int & in index )", VSP_FN( vsp::GetGeomSetCollection ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAttributeName( const string & in attrID )", VSP_FN( vsp::GetAttributeName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAttributeID( const string & in collID, const string & in attributeName, int index )", VSP_FN( vsp::GetAttributeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAttributeIDVec( const string & in collID, const string & in attributeName )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAttributeDoc( const string & in attrID )", VSP_FN( vsp::GetAttributeDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetAttributeType( const string & in attrID )", VSP_FN( vsp::GetAttributeType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetAttributeTypeName( const string & in attrID )", VSP_FN( vsp::GetAttributeTypeName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<bool>@+ GetAttributeBoolVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeBoolVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<int>@+ GetAttributeIntVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeIntVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetAttributeDoubleVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeDoubleVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAttributeStringVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeStringVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAttributeParmID( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeParmID ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetAttributeParmVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeParmVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAttributeParmName( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeParmName ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetAttributeVec3dVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeVec3dVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<array<int>>@+ GetAttributeIntMatrixVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeIntMatrixVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<array<double>>@+ GetAttributeDoubleMatrixVal( const string & in attrID )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAttributeDoubleMatrixVal ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeName( const string & in attrID, const string & in name )", VSP_FN( vsp::SetAttributeName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeDoc( const string & in attrID, const string & in doc )", VSP_FN( vsp::SetAttributeDoc ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeBool( const string & in attrID, bool value )", VSP_FN( vsp::SetAttributeBool ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeInt( const string & in attrID, int value )", VSP_FN( vsp::SetAttributeInt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeDouble( const string & in attrID, double value )", VSP_FN( vsp::SetAttributeDouble ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeString( const string & in attrID, const string & in value )", VSP_FN( vsp::SetAttributeString ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeParmID( const string & in attrID, const string & in value )", VSP_FN( vsp::SetAttributeParmID ) );
    assert( r >= 0 );

    // check if we need @+ on the input- may be bad idea
    r = se->RegisterGlobalFunction( "void SetAttributeVec3d( const string & in attrID, array<vec3d>@+ value )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAttributeVec3d ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeIntMatrix( const string & in attrID, array<array<int>>@+ value )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAttributeIntMatrix ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAttributeDoubleMatrix( const string & in attrID, array<array<double>>@+ value )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAttributeDoubleMatrix ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAttribute( const string & in attrID )", VSP_FN( vsp::DeleteAttribute ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeBool( const string & in collID, const string & in attributeName, bool value )", VSP_FN( vsp::AddAttributeBool ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeInt( const string & in collID, const string & in attributeName, int value )", VSP_FN( vsp::AddAttributeInt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeDouble( const string & in collID, const string & in attributeName, double value )", VSP_FN( vsp::AddAttributeDouble ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeString( const string & in collID, const string & in attributeName, const string & in value )", VSP_FN( vsp::AddAttributeString ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeParm( const string & in collID, const string & in attributeName, const string & in value )", VSP_FN( vsp::AddAttributeParm ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeVec3d( const string & in attrID, const string & in attributeName, const array<vec3d>@+ value )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddAttributeVec3d ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeIntMatrix( const string & in collID, const string & in attributeName, const array<array<int>>@+ value)", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddAttributeIntMatrix ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeDoubleMatrix( const string & in collID, const string & in attributeName, const array<array<double>>@+ value)", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddAttributeDoubleMatrix ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddAttributeGroup( const string & in collID, const string & in attributeName )", VSP_FN( vsp::AddAttributeGroup ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int CopyAttribute( const string & in attr_id )", VSP_FN( vsp::CopyAttribute ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CutAttribute( const string & in attr_id )", VSP_FN( vsp::CutAttribute ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ PasteAttribute( const string & in coll_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, PasteAttribute ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllResultsNames()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllResultsNames ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllDataNames(const string & in results_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllDataNames ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<int>@+ GetIntResults( const string & in id, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetIntResults ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetDoubleResults( const string & in id, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDoubleResults ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<array<double>@>@+ GetDoubleMatResults( const string & in id, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDoubleMatResults ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "array<string>@+ GetStringResults( const string & in id, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetStringResults ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetVec3dResults( const string & in id, const string & in name, int index = 0 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVec3dResults ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string CreateGeomResults( const string & in geom_id, const string & in name )", VSP_FN( vsp::CreateGeomResults ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllResults()", VSP_FN( vsp::DeleteAllResults ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteResult( const string & in id )", VSP_FN( vsp::DeleteResult ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteResultsCSVFile( const string & in id, const string & in file_name )", VSP_FN( vsp::WriteResultsCSVFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PrintResults( const string & in id )", VSP_FN( vsp::PrintResults ) );
    assert( r >= 0 );


    // The API and the Python binding both call this PrintResultsDocs.  Register
    // that name so the documentation examples agree across languages, and keep
    // the original singular spelling as an alias so existing scripts still run.
    r = se->RegisterGlobalFunction( "void PrintResultsDocs( const string & in id )", VSP_FN( vsp::PrintResultsDocs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PrintResultsDoc( const string & in id )", VSP_FN( vsp::PrintResultsDocs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteTestResults()", VSP_MFN_GLOBAL( ResultsMgrSingleton, WriteTestResults ), &ResultsMgr );
    assert( r >= 0 );


    //==== Geom Functions ====//

    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomTypes()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomTypes ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddGeom( const string & in type, const string & in parent = string() )", VSP_FN( vsp::AddGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void UpdateGeom(const string & in geom_id)", VSP_FN( vsp::UpdateGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteGeom(const string & in geom_id, int clone_delete = CLONE_DELETE_LEAVE_EMPTY)", VSP_FN( vsp::DeleteGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteGeomVec( array<string>@+ del_arr, int clone_delete = CLONE_DELETE_LEAVE_EMPTY )", VSP_MFN_GLOBAL( ScriptMgrSingleton, DeleteGeomVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CutGeomToClipboard(const string & in geom_id, int clone_delete = CLONE_DELETE_LEAVE_EMPTY)", VSP_FN( vsp::CutGeomToClipboard ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CopyGeomToClipboard(const string & in geom_id)", VSP_FN( vsp::CopyGeomToClipboard ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ PasteGeomClipboard( const string & in parent_id = \"\" )", VSP_MFN_GLOBAL( ScriptMgrSingleton, PasteGeomClipboard ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string ConvertFuselageToStack( const string & in geom_id )", VSP_FN( vsp::ConvertFuselageToStack ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindGeoms()", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindGeoms ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindGeomsWithName(const string & in name)", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindGeomsWithName ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindGeom(const string & in name, int index)", VSP_FN( vsp::FindGeom ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGeomCloneOriginal( const string & in clone_id, const string & in original_id )", VSP_FN( vsp::SetGeomCloneOriginal ) );
    assert( r >= 0 );
    r = se->RegisterGlobalFunction( "string GetGeomCloneOriginal( const string & in clone_id )", VSP_FN( vsp::GetGeomCloneOriginal ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeomCloneNameSuffix( const string & in clone_id, const string & in name_suffix )", VSP_FN( vsp::SetGeomCloneNameSuffix ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeomCloneNameSuffix( const string & in clone_id )", VSP_FN( vsp::GetGeomCloneNameSuffix ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string ReplaceCloneGeom( const string & in clone_id )", VSP_FN( vsp::ReplaceCloneGeom ) );
    assert( r >= 0 );
    r = se->RegisterGlobalFunction( "array<string>@+ CloneGeomVec( array<string>@+ geom_arr, const string & in name_suffix = \"_Clone\" )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CloneGeomVec ), &ScriptMgr );
    assert( r >= 0 );
    r = se->RegisterGlobalFunction( "array<string>@+ FindGeomClones( array<string>@+ geom_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindGeomClones ), &ScriptMgr );
    assert( r >= 0 );
    r = se->RegisterGlobalFunction( "void SetGeomName( const string & in geom_id, const string & in name )", VSP_FN( vsp::SetGeomName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGeomName( const string & in geom_id )", VSP_FN( vsp::GetGeomName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomParmIDs(const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomParmIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetGeomVSPSurfCfdType( const string & in geom_id, int main_surf_ind = 0 )", VSP_FN( vsp::GetGeomVSPSurfCfdType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetGeomVSPSurfType( const string & in geom_id, int main_surf_ind = 0 )", VSP_FN( vsp::GetGeomVSPSurfType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGeomTypeName(const string & in geom_id )", VSP_FN( vsp::GetGeomTypeName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumMainSurfs( const string & in geom_id )", VSP_FN( vsp::GetNumMainSurfs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetTotalNumSurfs( const string & in geom_id )", VSP_FN( vsp::GetTotalNumSurfs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d GetGeomBBoxMax( const string & in geom_id, int main_surf_ind = 0, bool ref_frame_is_absolute = true )", VSP_FN( vsp::GetGeomBBoxMax ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d GetGeomBBoxMin( const string & in geom_id, int main_surf_ind = 0, bool ref_frame_is_absolute = true )", VSP_FN( vsp::GetGeomBBoxMin ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGeomParent( const string & in geom_id, const string & in parent_id )", VSP_FN( vsp::SetGeomParent ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGeomParent( const string & in geom_id )", VSP_FN( vsp::GetGeomParent ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeomSymAncestorID( const string & in geom_id )", VSP_FN( vsp::GetGeomSymAncestorID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetGeomIncorporatedAncestorID( const string & in geom_id )", VSP_FN( vsp::GetGeomIncorporatedAncestorID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeomSymAncestorID( const string & in geom_id, const string & in ancestor_id )", VSP_FN( vsp::SetGeomSymAncestorID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetGeomIncorporatedAncestorID( const string & in geom_id, const string & in ancestor_id )", VSP_FN( vsp::SetGeomIncorporatedAncestorID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomChildren( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomChildren ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SplitWingXSec( const string & in wing_id, int section_index )", VSP_FN( vsp::SplitWingXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string SplitWingXSecByID( const string & in xsec_id )", VSP_FN( vsp::SplitWingXSecByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<int>@+ GetDriverGroup( const string & in geom_id, int section_index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDriverGroup ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetDriverGroup( const string & in geom_id, int section_index, int driver_0, int driver_1 = -1, int driver_2 = -1)", VSP_FN( vsp::SetDriverGroup ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetDriverGroupByID( const string & in xsec_id, int driver_0, int driver_1 = -1, int driver_2 = -1 )", VSP_FN( vsp::SetDriverGroupByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<int>@+ GetDriverGroupByID( const string & in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetDriverGroupByID ), &ScriptMgr );
    assert( r >= 0 );

    //==== SubSurface Functions ====//

    r = se->RegisterGlobalFunction( "string AddSubSurf( const string & in geom_id, int type, int surfindex = 0 )", VSP_FN( vsp::AddSubSurf ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteSubSurf( const string & in geom_id, const string & in sub_id )", VSP_FN_PR( vsp::DeleteSubSurf, ( const string &, const string & ), void ) );
    assert( r >= 0 );
    // TODO: Why are there two DeleteSubSurf if Geom ID isn't needed?

    r = se->RegisterGlobalFunction( "void DeleteSubSurf( const string & in sub_id )", VSP_FN_PR( vsp::DeleteSubSurf, ( const string & ), void ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSubSurf( const string & in geom_id, int index )", VSP_FN_PR( vsp::GetSubSurf, ( const string &, int ), string ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetSubSurf( const string & in geom_id, const string & in name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetSubSurf ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetSubSurfName( const string & in geom_id, const string & in sub_id, const string & in name )", VSP_FN_PR( vsp::SetSubSurfName, ( const string &, const string &, const string & ), void ) );
    assert( r >= 0 );
    // TODO: Why are there two SetSubSurfName if Geom ID isn't needed?

    r = se->RegisterGlobalFunction( "void SetSubSurfName( const string & in sub_id, const string & in name )", VSP_FN_PR( vsp::SetSubSurfName, ( const string &, const string & ), void ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSubSurfName( const string & in geom_id, const string & in sub_id )", VSP_FN_PR( vsp::GetSubSurfName, ( const string &, const string & ), string ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSubSurfName( const string & in sub_id )", VSP_FN_PR( vsp::GetSubSurfName, ( const string & ), string ) );
    assert( r >= 0 );
    // TODO: Why are there two GetSubSurfName if Geom ID isn't needed?

    r = se->RegisterGlobalFunction( "int GetSubSurfIndex( const string & in sub_id )", VSP_FN( vsp::GetSubSurfIndex ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetSubSurfIDVec( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetSubSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllSubSurfIDs()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllSubSurfIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumSubSurf( const string & in geom_id )", VSP_FN( vsp::GetNumSubSurf ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetSubSurfType( const string & in sub_id )", VSP_FN( vsp::GetSubSurfType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetSubSurfParmIDs(const string & in sub_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetSubSurfParmIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void IntersectSubSurf( const string & in sub_id )", VSP_FN( vsp::IntersectSubSurf ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetIntersectSubSurfGeomID( const string & in sub_id )", VSP_FN( vsp::GetIntersectSubSurfGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetIntersectSubSurfGeomID( const string & in sub_id, const string & in geom_id )", VSP_FN( vsp::SetIntersectSubSurfGeomID ) );
    assert( r >= 0 );


    //==== VSPAERO CS Group Functions ====//

    r = se->RegisterGlobalFunction("void AutoGroupVSPAEROControlSurfaces()", VSP_FN(vsp::AutoGroupVSPAEROControlSurfaces) );
    assert(r >= 0);


    r = se->RegisterGlobalFunction( "int GetNumControlSurfaceGroups()", VSP_FN( vsp::GetNumControlSurfaceGroups ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddCpSlice( int cut_type, double location )", VSP_FN( vsp::AddCpSlice ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumCpSlices()", VSP_FN( vsp::GetNumCpSlices ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetCpSliceID( int slice_index )", VSP_FN( vsp::GetCpSliceID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetCpSliceIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetCpSliceIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelCpSlice( int slice_index )", VSP_FN( vsp::DelCpSlice ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelCpSliceByID( const string & in slice_id )", VSP_FN( vsp::DelCpSliceByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllCpSlices()", VSP_FN( vsp::DeleteAllCpSlices ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction("void DeleteVSPAEROControlSurfaceGroup( int CSGroupIndex )", VSP_FN(vsp::DeleteVSPAEROControlSurfaceGroup) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteVSPAEROControlSurfaceGroupByID( const string & in group_id )", VSP_FN( vsp::DeleteVSPAEROControlSurfaceGroupByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction("int CreateVSPAEROControlSurfaceGroup()", VSP_FN(vsp::CreateVSPAEROControlSurfaceGroup) );
    assert(r >= 0);


    r = se->RegisterGlobalFunction("void AddAllToVSPAEROControlSurfaceGroup( int CSGroupIndex )", VSP_FN(vsp::AddAllToVSPAEROControlSurfaceGroup ) );
    assert(r >= 0);

    r = se->RegisterGlobalFunction( "string AddVSPAEROControlSurfaceGroup()", VSP_FN( vsp::AddVSPAEROControlSurfaceGroup ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddAllToVSPAEROControlSurfaceGroupByID( const string & in group_id )", VSP_FN( vsp::AddAllToVSPAEROControlSurfaceGroupByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction("void RemoveAllFromVSPAEROControlSurfaceGroup( int CSGroupIndex )", VSP_FN(vsp::RemoveAllFromVSPAEROControlSurfaceGroup ) );
    assert(r >= 0);

    r = se->RegisterGlobalFunction( "void RemoveAllFromVSPAEROControlSurfaceGroupByID( const string & in group_id )", VSP_FN( vsp::RemoveAllFromVSPAEROControlSurfaceGroupByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetActiveCSNameVec( int CSGroupIndex )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetActiveCSNameVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetActiveCSNameVecByID( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetActiveCSNameVecByID ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetActiveCSSubSurfIDVec( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetActiveCSSubSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<int>@+ GetActiveCSReflectVec( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetActiveCSReflectVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetCompleteCSNameVec( )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetCompleteCSNameVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetCompleteCSSubSurfIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetCompleteCSSubSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<int>@+ GetCompleteCSReflectVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetCompleteCSReflectVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAvailableCSNameVec( int CSGroupIndex )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAvailableCSNameVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAvailableCSNameVecByID( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAvailableCSNameVecByID ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAvailableCSSubSurfIDVec( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAvailableCSSubSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<int>@+ GetAvailableCSReflectVec( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAvailableCSReflectVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction("void SetVSPAEROControlGroupName( const string & in name, int CSGroupIndex )", VSP_FN(vsp::SetVSPAEROControlGroupName) );
    assert(r >= 0);

    r = se->RegisterGlobalFunction( "void SetVSPAEROControlGroupNameByID( const string & in name, const string & in group_id )", VSP_FN( vsp::SetVSPAEROControlGroupNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPAEROControlGroupName( int CSGroupIndex )", VSP_FN( vsp::GetVSPAEROControlGroupName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetVSPAEROControlGroupNameByID( const string & in group_id )", VSP_FN( vsp::GetVSPAEROControlGroupNameByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddSubSurfToCSGroup( const string & in group_id, const string & in ss_id, int reflect_index )", VSP_FN( vsp::AddSubSurfToCSGroup ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RemoveSubSurfFromCSGroup( const string & in group_id, const string & in ss_id, int reflect_index )", VSP_FN( vsp::RemoveSubSurfFromCSGroup ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddSelectedToCSGroup( array<int>@+ selected, int CSGroupIndex )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddSelectedToCSGroup ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void RemoveSelectedFromCSGroup( array<int>@+ selected, int CSGroupIndex )", VSP_MFN_GLOBAL( ScriptMgrSingleton, RemoveSelectedFromCSGroup ), &ScriptMgr );
    assert( r >= 0 ); // FIXME: RemoveSelectedFromCSGroup not working

    //==== VSPAERO Functions ====//

    r = se->RegisterGlobalFunction( "string GetVSPAERORefWingID()", VSP_FN( vsp::GetVSPAERORefWingID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string SetVSPAERORefWingID( const string & in geom_id )", VSP_FN( vsp::SetVSPAERORefWingID ) );
    assert( r >= 0 );

    //==== VSPAERO Disk and Prop Functions ====//

    r = se->RegisterGlobalFunction( "string FindActuatorDisk( int disk_index )", VSP_FN( vsp::FindActuatorDisk ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string FindActuatorDiskByGeom( const string & in geom_id, int surf_index )", VSP_FN( vsp::FindActuatorDiskByGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetActuatorDiskGeomID( const string & in disk_id )", VSP_FN( vsp::GetActuatorDiskGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumLights()", VSP_FN( vsp::GetNumLights ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string FindLight( int index )", VSP_FN( vsp::FindLight ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetLightIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetLightIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string FindControlSurfaceGroup( int group_index )", VSP_FN( vsp::FindControlSurfaceGroup ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetControlSurfaceGroupIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetControlSurfaceGroupIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaAssemblyConnectionID( const string & in assembly_id, int connection_index )", VSP_FN( vsp::GetFeaAssemblyConnectionID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaConnectionStartFixPtID( const string & in connection_id )", VSP_FN( vsp::GetFeaConnectionStartFixPtID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaConnectionStartStructID( const string & in connection_id )", VSP_FN( vsp::GetFeaConnectionStartStructID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaConnectionEndFixPtID( const string & in connection_id )", VSP_FN( vsp::GetFeaConnectionEndFixPtID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaConnectionEndStructID( const string & in connection_id )", VSP_FN( vsp::GetFeaConnectionEndStructID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumActuatorDisks()", VSP_FN( vsp::GetNumActuatorDisks ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetActuatorDiskIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetActuatorDiskIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindUnsteadyGroup( int group_index )", VSP_FN( vsp::FindUnsteadyGroup ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetUnsteadyGroupName( int group_index )", VSP_FN( vsp::GetUnsteadyGroupName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetUnsteadyGroupNameByID( const string & in group_id )", VSP_FN( vsp::GetUnsteadyGroupNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetUnsteadyGroupCompIDs( int group_index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUnsteadyGroupCompIDs ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetUnsteadyGroupCompIDsByID( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUnsteadyGroupCompIDsByID ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<int>@+ GetUnsteadyGroupSurfIndexes( int group_index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUnsteadyGroupSurfIndexes ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<int>@+ GetUnsteadyGroupSurfIndexesByID( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUnsteadyGroupSurfIndexesByID ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumUnsteadyGroups()", VSP_FN( vsp::GetNumUnsteadyGroups ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetUnsteadyGroupIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUnsteadyGroupIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumUnsteadyRotorGroups()", VSP_FN( vsp::GetNumUnsteadyRotorGroups ) );
    assert( r >= 0 );

    //==== XSecSurf Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumXSecSurfs( const string & in geom_id )", VSP_FN( vsp::GetNumXSecSurfs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetXSecSurf( const string & in geom_id, int index )", VSP_FN( vsp::GetXSecSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetXSecSurfIDVec( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumXSec( const string & in xsec_surf_id )", VSP_FN( vsp::GetNumXSec ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetXSec( const string & in xsec_surf_id, int xsec_index )", VSP_FN( vsp::GetXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetXSecIDVec( const string & in xsec_surf_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetEngineInletFaceXSecID( const string & in geom_id, const string & in xsec_id )", VSP_FN( vsp::SetEngineInletFaceXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetEngineInletFaceXSecID( const string & in geom_id )", VSP_FN( vsp::GetEngineInletFaceXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetEngineInletLipXSecID( const string & in geom_id, const string & in xsec_id )", VSP_FN( vsp::SetEngineInletLipXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetEngineInletLipXSecID( const string & in geom_id )", VSP_FN( vsp::GetEngineInletLipXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetEngineOutletFaceXSecID( const string & in geom_id, const string & in xsec_id )", VSP_FN( vsp::SetEngineOutletFaceXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetEngineOutletFaceXSecID( const string & in geom_id )", VSP_FN( vsp::GetEngineOutletFaceXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetEngineOutletLipXSecID( const string & in geom_id, const string & in xsec_id )", VSP_FN( vsp::SetEngineOutletLipXSecID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetEngineOutletLipXSecID( const string & in geom_id )", VSP_FN( vsp::GetEngineOutletLipXSecID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ChangeXSecShape( const string & in xsec_surf_id, int xsec_index, int type )", VSP_FN( vsp::ChangeXSecShape ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ChangeXSecShapeByID( const string & in xsec_id, int type )", VSP_FN( vsp::ChangeXSecShapeByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void FitCSTAirfoil( const string & in xsec_surf_id, int xsec_index, int deg )", VSP_FN( vsp::FitCSTAirfoil ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void FitCSTAirfoilByID( const string & in xsec_id, int deg )", VSP_FN( vsp::FitCSTAirfoilByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecSurfGlobalXForm( const string & in xsec_surf_id, const Matrix4d & in mat )", VSP_FN( vsp::SetXSecSurfGlobalXForm ) );
    assert( r >= 0 ); // TODO: Add Example


    r = se->RegisterGlobalFunction( "Matrix4d GetXSecSurfGlobalXForm( const string & in xsec_surf_id )", VSP_FN( vsp::GetXSecSurfGlobalXForm ) );
    assert( r >= 0 ); // TODO: Add Example

    //==== XSec Functions ====//

    r = se->RegisterGlobalFunction( "void SetXSecAlias( const string & in id, const string & in alias )", VSP_FN( vsp::SetXSecAlias ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetXSecAlias( const string & in id )", VSP_FN( vsp::GetXSecAlias ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecCurveAlias( const string & in id, const string & in alias )", VSP_FN( vsp::SetXSecCurveAlias ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetXSecCurveAlias( const string & in id )", VSP_FN( vsp::GetXSecCurveAlias ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CutXSec( const string & in geom_id, int index )", VSP_FN( vsp::CutXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void CutXSecByID( const string & in xsec_id )", VSP_FN( vsp::CutXSecByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CopyXSec( const string & in geom_id, int index )", VSP_FN( vsp::CopyXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void CopyXSecByID( const string & in xsec_id )", VSP_FN( vsp::CopyXSecByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void PasteXSec( const string & in geom_id, int index )", VSP_FN( vsp::PasteXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void PasteXSecByID( const string & in xsec_id )", VSP_FN( vsp::PasteXSecByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void InsertXSec( const string & in geom_id, int index, int type )", VSP_FN( vsp::InsertXSec ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string InsertXSecByID( const string & in xsec_id, int type )", VSP_FN( vsp::InsertXSecByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetXSecShape( const string& in xsec_id )", VSP_FN( vsp::GetXSecShape ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetXSecWidth( const string& in xsec_id )", VSP_FN( vsp::GetXSecWidth ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetXSecHeight( const string& in xsec_id )", VSP_FN( vsp::GetXSecHeight ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecWidth( const string& in xsec_id, double w )", VSP_FN( vsp::SetXSecWidth ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecHeight( const string& in xsec_id, double h )", VSP_FN( vsp::SetXSecHeight ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecWidthHeight( const string& in xsec_id, double w, double h )", VSP_FN( vsp::SetXSecWidthHeight ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetXSecParmIDs(const string & in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecParmIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetXSecParm( const string& in xsec_id, const string& in name )", VSP_FN( vsp::GetXSecParm ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ ReadFileXSec(const string& in xsec_id, const string& in file_name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ReadFileXSec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetXSecPnts( const string& in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecPnts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void CopyXSecCurve( const string & in geom_id, int index )", VSP_FN( vsp::CopyXSecCurve ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void CopyXSecCurveByID( const string & in xsec_id )", VSP_FN( vsp::CopyXSecCurveByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void PasteXSecCurve( const string & in geom_id, int index )", VSP_FN( vsp::PasteXSecCurve ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void PasteXSecCurveByID( const string & in xsec_id )", VSP_FN( vsp::PasteXSecCurveByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetXSecPnts( const string& in xsec_id, array<vec3d>@+ pnt_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetXSecPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d ComputeXSecPnt( const string& in xsec_id, double fract )", VSP_FN( vsp::ComputeXSecPnt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d ComputeXSecTan( const string& in xsec_id, double fract )", VSP_FN( vsp::ComputeXSecTan ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ResetXSecSkinParms( const string& in xsec_id )", VSP_FN( vsp::ResetXSecSkinParms ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetXSecContinuity( const string& in xsec_id )", VSP_FN( vsp::GetXSecContinuity ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetXSecContinuity( const string& in xsec_id, int cx )", VSP_FN( vsp::SetXSecContinuity ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int AddSkinSpine( const string& in geom_id, double w01 )", VSP_FN( vsp::AddSkinSpine ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelSkinSpine( const string& in geom_id, int index )", VSP_FN( vsp::DelSkinSpine ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelSkinSpineByID( const string & in spine_id )", VSP_FN( vsp::DelSkinSpineByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllSkinSpines( const string& in geom_id )", VSP_FN( vsp::DelAllSkinSpines ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumSkinSpines( const string& in geom_id )", VSP_FN( vsp::GetNumSkinSpines ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetSkinSpineName( const string& in geom_id, int index )", VSP_FN( vsp::GetSkinSpineName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetSkinSpineNameByID( const string & in spine_id )", VSP_FN( vsp::GetSkinSpineNameByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetSkinSpineName( const string& in geom_id, int index, const string& in name )", VSP_FN( vsp::SetSkinSpineName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetSkinSpineNameByID( const string & in spine_id, const string & in name )", VSP_FN( vsp::SetSkinSpineNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSkinSpineID( const string& in xsec_id, int index )", VSP_FN( vsp::GetSkinSpineID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetSkinSpineIDVec( const string & in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetSkinSpineIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetSkinSpineParm( const string& in spine_id, const string& in name )", VSP_FN( vsp::GetSkinSpineParm ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetXSecTanAngles( const string& in xsec_id, int side )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecTanAngles ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<double>@+ GetXSecTanSlews( const string& in xsec_id, int side )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecTanSlews ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<double>@+ GetXSecTanStrengths( const string& in xsec_id, int side )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecTanStrengths ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<double>@+ GetXSecCurvatures( const string& in xsec_id, int side )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetXSecCurvatures ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetXSecTanAngles( const string& in xsec_id, int side, double top, double right = -1.0e12, double bottom = -1.0e12, double left = -1.0e12 )", VSP_FN( vsp::SetXSecTanAngles ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecTanSlews( const string& in xsec_id, int side, double top, double right = -1.0e12, double bottom = -1.0e12, double left = -1.0e12 )", VSP_FN( vsp::SetXSecTanSlews ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecTanStrengths( const string& in xsec_id, int side, double top, double right = -1.0e12, double bottom = -1.0e12, double left = -1.0e12 )", VSP_FN( vsp::SetXSecTanStrengths ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetXSecCurvatures( const string& in xsec_id, int side, double top, double right = -1.0e12, double bottom = -1.0e12, double left = -1.0e12 )", VSP_FN( vsp::SetXSecCurvatures ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ReadFileAirfoil( const string& in xsec_id, const string& in file_name )", VSP_FN( vsp::ReadFileAirfoil ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAirfoilUpperPnts( const string& in xsec_id, array<vec3d>@+ up_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAirfoilUpperPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAirfoilLowerPnts( const string& in xsec_id, array<vec3d>@+ low_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAirfoilLowerPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetAirfoilPnts( const string& in xsec_id, array<vec3d>@+ up_pnt_vec, array<vec3d>@+ low_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetAirfoilPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetHersheyBarLiftDist( const int& in npts, const double& in alpha, const double& in Vinf, const double& in span, bool full_span_flag = false )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetHersheyBarLiftDist ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetHersheyBarDragDist( const int& in npts, const double& in alpha, const double& in Vinf, const double& in span, bool full_span_flag = false )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetHersheyBarDragDist ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetVKTAirfoilPnts( const int& in npts, const double& in alpha, const double& in epsilon, const double& in kappa, const double& in tau )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVKTAirfoilPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetVKTAirfoilCpDist( const double& in alpha, const double& in epsilon, const double& in kappa, const double& in tau, array<vec3d>@+ xydata )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVKTAirfoilCpDist ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double IntegrateEllipsoidFlow( const vec3d & in abc_rad, const int & in abc_index )", VSP_FN( vsp::IntegrateEllipsoidFlow ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetEllipsoidSurfPnts( const vec3d& in center, const vec3d& in abc_rad, int u_npts = 20, int w_npts = 20 )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetEllipsoidSurfPnts ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetFeatureLinePnts( const string& in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeatureLinePnts ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "array<double>@+ GetEllipsoidCpDist( array<vec3d>@+ surf_pnt_arr, const vec3d& in abc_rad, const vec3d& in V_inf )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetEllipsoidCpDist ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetAirfoilUpperPnts(const string& in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAirfoilUpperPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetAirfoilLowerPnts(const string& in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAirfoilLowerPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetUpperCSTCoefs( const string & in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUpperCSTCoefs ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "array<double>@+ GetLowerCSTCoefs( const string & in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetLowerCSTCoefs ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "int GetUpperCSTDegree( const string& in xsec_id )", VSP_FN( vsp::GetUpperCSTDegree ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "int GetLowerCSTDegree( const string& in xsec_id )", VSP_FN( vsp::GetLowerCSTDegree ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void SetUpperCST( const string& in xsec_id, int deg, array<double>@+ coeff_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetUpperCST ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void SetLowerCST( const string& in xsec_id, int deg, array<double>@+ coeff_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetLowerCST ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void PromoteCSTUpper( const string& in xsec_id )", VSP_FN( vsp::PromoteCSTUpper ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void PromoteCSTLower( const string& in xsec_id )", VSP_FN( vsp::PromoteCSTLower ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void DemoteCSTUpper( const string& in xsec_id )", VSP_FN( vsp::DemoteCSTUpper ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void DemoteCSTLower( const string& in xsec_id )", VSP_FN( vsp::DemoteCSTLower ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void FitAfCST( const string& in xsec_surf_id, int xsec_index, int deg )", VSP_FN( vsp::FitAfCST ) );
    assert( r >= 0 );  // TODO: Example

    r = se->RegisterGlobalFunction( "void FitAfCSTByID( const string & in xsec_id, int deg )", VSP_FN( vsp::FitAfCSTByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteBezierAirfoil( const string& in file_name, const string& in geom_id, const double& in foilsurf_u )", VSP_FN( vsp::WriteBezierAirfoil ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteSeligAirfoil( const string& in file_name, const string& in geom_id, const double& in foilsurf_u )", VSP_FN( vsp::WriteSeligAirfoil ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetAirfoilCoordinates( const string& in geom_id, const double& in foilsurf_u )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAirfoilCoordinates ), &ScriptMgr );
    assert( r >= 0 );

    //==== Edit Curve XSec Functions ====//

    r = se->RegisterGlobalFunction( "void EditXSecInitShape( const string& in xsec_id )", VSP_FN( vsp::EditXSecInitShape ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void EditXSecConvertTo( const string& in xsec_id, const int& in newtype )", VSP_FN( vsp::EditXSecConvertTo ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetEditXSecUVec( const string& in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetEditXSecUVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetEditXSecCtrlVec( const string& in xsec_id, const bool non_dimensional = true )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetEditXSecCtrlVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetEditXSecPnts( const string& in xsec_id, array<double>@+ u_vec, array<vec3d>@+ control_pts, array<double>@+ r_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetEditXSecPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void EditXSecDelPnt( const string& in xsec_id, const int& in indx )", VSP_FN( vsp::EditXSecDelPnt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int EditXSecSplit01( const string& in xsec_id, const double& in u )", VSP_FN( vsp::EditXSecSplit01 ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void MoveEditXSecPnt( const string& in xsec_id, const int& in indx, const vec3d& in new_pnt )", VSP_FN( vsp::MoveEditXSecPnt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertXSecToEdit( const string& in geom_id, const int& in indx = 0 )", VSP_FN( vsp::ConvertXSecToEdit ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ConvertXSecToEditByID( const string & in xsec_id )", VSP_FN( vsp::ConvertXSecToEditByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<bool>@+ GetEditXSecFixedUVec( const string& in xsec_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetEditXSecFixedUVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetEditXSecFixedUVec( const string& in xsec_id, array<bool>@+ fixed_u_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetEditXSecFixedUVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ReparameterizeEditXSec( const string& in xsec_id )", VSP_FN( vsp::ReparameterizeEditXSec ) );
    assert( r >= 0 );

    //==== Background3D Functions ====//

    r = se->RegisterGlobalFunction( "string AddBackground3D()", VSP_FN( vsp::AddBackground3D ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumBackground3Ds()", VSP_FN( vsp::GetNumBackground3Ds ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllBackground3Ds()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllBackground3Ds ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowAllBackground3Ds()", VSP_FN( vsp::ShowAllBackground3Ds ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void HideAllBackground3Ds()", VSP_FN( vsp::HideAllBackground3Ds ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllBackground3Ds()", VSP_FN( vsp::DelAllBackground3Ds ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelBackground3D( const string & in id )", VSP_FN( vsp::DelBackground3D ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllBackground3DRelativePaths()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllBackground3DRelativePaths ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllBackground3DAbsolutePaths()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllBackground3DAbsolutePaths ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetBackground3DRelativePath( const string & in id )", VSP_FN( vsp::GetBackground3DRelativePath ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetBackground3DAbsolutePath( const string & in id )", VSP_FN( vsp::GetBackground3DAbsolutePath ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBackground3DRelativePath( const string & in id, const string & in fname )", VSP_FN( vsp::SetBackground3DRelativePath ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBackground3DAbsolutePath( const string & in id, const string & in fname )", VSP_FN( vsp::SetBackground3DAbsolutePath ) );
    assert( r >= 0 );

    //==== RoutingGeom Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumRoutingPts( const string & in routing_id )", VSP_FN( vsp::GetNumRoutingPts ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddRoutingPt( const string & in routing_id, const string & in geom_id, int surf_index )", VSP_FN( vsp::AddRoutingPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string InsertRoutingPt( const string & in routing_id, int index, const string & in geom_id, int surf_index )", VSP_FN( vsp::InsertRoutingPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string InsertRoutingPtByID( const string & in routing_id, const string & in before_pt_id, const string & in geom_id, int surf_index )", VSP_FN( vsp::InsertRoutingPtByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelRoutingPt( const string & in routing_id, int index  )", VSP_FN( vsp::DelRoutingPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelRoutingPtByID( const string & in routing_id, const string & in pt_id )", VSP_FN( vsp::DelRoutingPtByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllRoutingPt( const string & in routing_id )", VSP_FN( vsp::DelAllRoutingPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int MoveRoutingPt( const string & in routing_id, int index, int reorder_type )", VSP_FN( vsp::MoveRoutingPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int MoveRoutingPtByID( const string & in routing_id, const string & in pt_id, int reorder_type )", VSP_FN( vsp::MoveRoutingPtByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetRoutingPtID( const string & in routing_id, int index )", VSP_FN( vsp::GetRoutingPtID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllRoutingPtIds( const string & in routing_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllRoutingPtIds ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetRoutingPtParentID( const string & in pt_id )", VSP_FN( vsp::GetRoutingPtParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetRoutingPtParentID( const string & in pt_id, const string & in parent_id )", VSP_FN( vsp::SetRoutingPtParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d GetMainRoutingPtCoord( const string & in pt_id )", VSP_FN( vsp::GetMainRoutingPtCoord ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d GetRoutingPtCoord( const string & in routing_id, int index, int symm_index )", VSP_FN( vsp::GetRoutingPtCoord ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d GetRoutingPtCoordByID( const string & in routing_id, const string & in pt_id, int symm_index )", VSP_FN( vsp::GetRoutingPtCoordByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetAllRoutingPtCoords( const string & in routing_id, int symm_index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllRoutingPtCoords ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetRoutingCurve( const string & in routing_id, int symm_index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetRoutingCurve ), &ScriptMgr );
    assert( r >= 0 );

    //==== BOR Functions ====//

    r = se->RegisterGlobalFunction( "void ChangeBORXSecShape( const string & in geom_id, int type )", VSP_FN( vsp::ChangeBORXSecShape ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetBORXSecShape( const string & in geom_id )", VSP_FN( vsp::GetBORXSecShape ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ ReadBORFileXSec(const string& in bor_id, const string& in file_name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ReadBORFileXSec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetBORXSecPnts( const string& in bor_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetBORXSecPnts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetBORXSecPnts( const string& in bor_id, array<vec3d>@+ pnt_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORXSecPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d ComputeBORXSecPnt( const string& in bor_id, double fract )", VSP_FN( vsp::ComputeBORXSecPnt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d ComputeBORXSecTan( const string& in bor_id, double fract )", VSP_FN( vsp::ComputeBORXSecTan ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ReadBORFileAirfoil( const string& in bor_id, const string& in file_name )", VSP_FN( vsp::ReadBORFileAirfoil ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetBORAirfoilUpperPnts( const string& in bor_id, array<vec3d>@+ up_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORAirfoilUpperPnts ), &ScriptMgr );
    assert( r >= 0 );



    r = se->RegisterGlobalFunction( "void SetBORAirfoilLowerPnts( const string& in bor_id, array<vec3d>@+ low_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORAirfoilLowerPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetBORAirfoilPnts( const string& in bor_id, array<vec3d>@+ up_pnt_vec, array<vec3d>@+ low_pnt_vec )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORAirfoilPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetBORAirfoilUpperPnts(const string& in bor_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetBORAirfoilUpperPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ GetBORAirfoilLowerPnts(const string& in bor_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetBORAirfoilLowerPnts ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetBORUpperCSTCoefs( const string & in bor_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetBORUpperCSTCoefs ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "array<double>@+ GetBORLowerCSTCoefs( const string & in bor_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetBORLowerCSTCoefs ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "int GetBORUpperCSTDegree( const string& in bor_id )", VSP_FN( vsp::GetBORUpperCSTDegree ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "int GetBORLowerCSTDegree( const string& in bor_id )", VSP_FN( vsp::GetBORLowerCSTDegree ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void SetBORUpperCST( const string& in bor_id, int deg, array<double>@+ coeff_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORUpperCST ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void SetBORLowerCST( const string& in bor_id, int deg, array<double>@+ coeff_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetBORLowerCST ), &ScriptMgr );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void PromoteBORCSTUpper( const string& in bor_id )", VSP_FN( vsp::PromoteBORCSTUpper ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void PromoteBORCSTLower( const string& in bor_id )", VSP_FN( vsp::PromoteBORCSTLower ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void DemoteBORCSTUpper( const string& in bor_id )", VSP_FN( vsp::DemoteBORCSTUpper ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void DemoteBORCSTLower( const string& in bor_id )", VSP_FN( vsp::DemoteBORCSTLower ) );
    assert( r >= 0 );  // TODO: Example


    r = se->RegisterGlobalFunction( "void FitBORAfCST( const string& in bor_id, int deg )", VSP_FN( vsp::FitBORAfCST ) );
    assert( r >= 0 );  // TODO: Example

    //==== Sets Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumSets()", VSP_FN( vsp::GetNumSets ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetSetName( int index, const string& in name )", VSP_FN( vsp::SetSetName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSetName( int index )", VSP_FN( vsp::GetSetName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomSetAtIndex( int index )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomSetAtIndex ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetGeomSet( const string & in name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetGeomSet ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetSetIndex( const string & in name )", VSP_FN( vsp::GetSetIndex ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool GetSetFlag( const string & in geom_id, int set_index )", VSP_FN( vsp::GetSetFlag ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetSetFlag( const string & in geom_id, int set_index, bool flag )", VSP_FN( vsp::SetSetFlag ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CopyPasteSet(  int copyIndex, int pasteIndex  )", VSP_FN( vsp::CopyPasteSet ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool GetBBoxSet( int set, double & out xmin_out, double & out ymin_out, double & out zmin_out, double & out xlen_out, double & out ylen_out, double & out zlen_out )", VSP_FN( vsp::GetBBoxSet ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool GetScaleIndependentBBoxSet( int set, double & out xmin_out, double & out ymin_out, double & out zmin_out, double & out xlen_out, double & out ylen_out, double & out zlen_out )", VSP_FN( vsp::GetScaleIndependentBBoxSet ) );
    assert( r >= 0 );

    //=== Group Modifications ===//

    r = se->RegisterGlobalFunction( "void ScaleSet( int set_index, double scale )", VSP_FN( vsp::ScaleSet ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void RotateSet( int set_index, double x_rot_deg, double y_rot_deg, double z_rot_deg )", VSP_FN( vsp::RotateSet ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void TranslateSet( int set_index, const vec3d & in translation_vec )", VSP_FN( vsp::TranslateSet ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void TransformSet( int set_index, const vec3d & in translation_vec, double x_rot_deg, double y_rot_deg, double z_rot_deg, double scale, bool scale_translations_flag )", VSP_FN( vsp::TransformSet ) );
    assert( r >= 0 );

    //==== Parm Functions ====//

    r = se->RegisterGlobalFunction( "bool ValidParm( const string & in id )", VSP_FN( vsp::ValidParm ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SetParmVal(const string & in parm_id, double val )",
                                    VSP_FN_PR( vsp::SetParmVal, ( const string &, double val ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SetParmValLimits(const string & in parm_id, double val, double lower_limit, double upper_limit )",
                                    VSP_FN( vsp::SetParmValLimits ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SetParmValUpdate(const string & in parm_id, double val )",
                                    VSP_FN_PR( vsp::SetParmValUpdate, ( const string &, double val ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SetParmVal(const string & in container_id, const string & in name, const string & in group, double val )",
                                    VSP_FN_PR( vsp::SetParmVal, ( const string &, const string &, const string &, double val ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SetParmValUpdate(const string & in container_id, const string & in parm_name, const string & in parm_group_name, double val )",
                                    VSP_FN_PR( vsp::SetParmValUpdate, ( const string &, const string &, const string &, double val ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetParmVal(const string & in parm_id )", VSP_FN_PR( vsp::GetParmVal, ( const string & ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetParmVal(const string & in container_id, const string & in name, const string & in group )",
                                    VSP_FN_PR( vsp::GetParmVal, ( const string &, const string &, const string & ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetIntParmVal(const string & in parm_id )", VSP_FN( vsp::GetIntParmVal ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool GetBoolParmVal(const string & in parm_id )", VSP_FN( vsp::GetBoolParmVal ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetParmUpperLimit( const string & in parm_id, double val )", VSP_FN( vsp::SetParmUpperLimit ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetParmUpperLimit( const string & in parm_id )", VSP_FN( vsp::GetParmUpperLimit ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetParmLowerLimit( const string & in parm_id, double val )", VSP_FN( vsp::SetParmLowerLimit ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetParmLowerLimit( const string & in parm_id )", VSP_FN( vsp::GetParmLowerLimit ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetParmType( const string & in parm_id )", VSP_FN( vsp::GetParmType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParmName( const string & in parm_id )", VSP_FN( vsp::GetParmName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParmGroupName( const string & in parm_id )", VSP_FN( vsp::GetParmGroupName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParmDisplayGroupName( const string & in parm_id )", VSP_FN( vsp::GetParmDisplayGroupName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParmContainer( const string & in parm_id )", VSP_FN( vsp::GetParmContainer ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetParmDescript( const string & in parm_id, const string & in desc )", VSP_FN( vsp::SetParmDescript ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParmDescript( const string & in parm_id )", VSP_FN( vsp::GetParmDescript ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindParm( const string & in parm_container_id, const string & in parm_name, const string & in group_name )", VSP_FN( vsp::FindParm ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetParm(const string & in container_id, const string & in name, const string & in group )", VSP_FN( vsp::GetParm ) );
    assert( r >= 0 );

    //=== Parm Container Functions ===//

    r = se->RegisterGlobalFunction( "array<string>@+ FindContainers()", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindContainers ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindContainersWithName( const string & in name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindContainersWithName ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string FindContainer( const string & in name, int index )", VSP_FN( vsp::FindContainer ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetContainerName( const string & in parm_container_id, const string & in name )", VSP_FN( vsp::SetContainerName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetContainerName( const string & in parm_container_id )", VSP_FN( vsp::GetContainerName ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindContainerGroupNames( const string & in parm_container_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindContainerGroupNames ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ FindContainerParmIDs( const string & in parm_container_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindContainerParmIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVehicleID()", VSP_FN( vsp::GetVehicleID ));
    assert( r >= 0 );

    //=== Register User Parm Functions ====//

    r = se->RegisterGlobalFunction( "int GetNumUserParms()", VSP_FN( vsp::GetNumUserParms ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumPredefinedUserParms()", VSP_FN( vsp::GetNumPredefinedUserParms ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllUserParms()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllUserParms ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetUserParmContainer()", VSP_FN( vsp::GetUserParmContainer ));
    assert( r >= 0 );



    r = se->RegisterGlobalFunction( "string AddUserParm( int type, const string & in name, const string & in group )",
                                    VSP_FN( vsp::AddUserParm ) );
    assert( r );


    r = se->RegisterGlobalFunction( "void DeleteUserParm( const string & in parm_id)", VSP_FN( vsp::DeleteUserParm ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllUserParm()", VSP_FN( vsp::DeleteAllUserParm ));
    assert( r >= 0 );

    //=== Register Snap To Functions ====//

    r = se->RegisterGlobalFunction( "double ComputeMinClearanceDistance( const string & in geom_id, int set, bool useMode = false, const string & in modeID = string() )", VSP_FN( vsp::ComputeMinClearanceDistance ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double SnapParm( const string & in parm_id, double target_min_dist, bool inc_flag, int set, bool useMode = false, const string & in modeID = string() )", VSP_FN( vsp::SnapParm ) );
    assert( r >= 0 );

    //=== Register Point Cloud Functions ====//

    r = se->RegisterGlobalFunction( "string CreatePtCloudGeomFromPts( array<vec3d>@+ pt_arr, const string & in name )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CreatePtCloudGeomFromPts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsInBBox( array<vec3d>@+ pt_arr, const vec3d & in min_pt, const vec3d & in max_pt )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsInBBox ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ RemovePtsInBBox( array<vec3d>@+ pt_arr, const vec3d & in min_pt, const vec3d & in max_pt )", VSP_MFN_GLOBAL( ScriptMgrSingleton, RemovePtsInBBox ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsInRange( array<vec3d>@+ pt_arr, int dir_index, double low, double high )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsInRange ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ RemovePtsInRange( array<vec3d>@+ pt_arr, int dir_index, double low, double high )", VSP_MFN_GLOBAL( ScriptMgrSingleton, RemovePtsInRange ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsAbove( array<vec3d>@+ pt_arr, int dir_index, double val )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsAbove ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsBelow( array<vec3d>@+ pt_arr, int dir_index, double val )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsBelow ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsNearPt( array<vec3d>@+ pt_arr, const vec3d & in center, double radius )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsNearPt ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ RemovePtsNearPt( array<vec3d>@+ pt_arr, const vec3d & in center, double radius )", VSP_MFN_GLOBAL( ScriptMgrSingleton, RemovePtsNearPt ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ KeepPtsNearGeom( array<vec3d>@+ pt_arr, const string & in geom_id, int surf_indx, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, KeepPtsNearGeom ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ RemovePtsNearGeom( array<vec3d>@+ pt_arr, const string & in geom_id, int surf_indx, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, RemovePtsNearGeom ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ UniquePts( array<vec3d>@+ pt_arr, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, UniquePts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ UnionPts( array<vec3d>@+ pt_arr_a, array<vec3d>@+ pt_arr_b, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, UnionPts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ IntersectPts( array<vec3d>@+ pt_arr_a, array<vec3d>@+ pt_arr_b, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, IntersectPts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<vec3d>@+ SubtractPts( array<vec3d>@+ pt_arr_a, array<vec3d>@+ pt_arr_b, double tol )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SubtractPts ), &ScriptMgr );
    assert( r >= 0 );

    //=== Register Fit Model Functions ====//

    r = se->RegisterGlobalFunction( "void ResetFitModel()", VSP_FN( vsp::ResetFitModel ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int AddFitModelTargetPt( const string & in geom_id, int surf_indx, const vec3d & in pt, int u_type = FIT_MODEL_FREE, int w_type = FIT_MODEL_FREE, double u = 0.0, double w = 0.0 )", VSP_FN( vsp::AddFitModelTargetPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int AddFitModelTargetPtFixedU( const string & in geom_id, int surf_indx, const vec3d & in pt, double u )", VSP_FN( vsp::AddFitModelTargetPtFixedU ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int AddFitModelTargetPtFixedW( const string & in geom_id, int surf_indx, const vec3d & in pt, double w )", VSP_FN( vsp::AddFitModelTargetPtFixedW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int AddFitModelTargetPtFixedUW( const string & in geom_id, int surf_indx, const vec3d & in pt, double u, double w )", VSP_FN( vsp::AddFitModelTargetPtFixedUW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPts( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPts ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedU( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, double u )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedU ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedUs( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, array<double>@+ u_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedUs ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedW( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, double w )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedW ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedWs( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, array<double>@+ w_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedWs ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedUW( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, double u, double w )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedUW ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelTargetPtsFixedUWs( const string & in geom_id, int surf_indx, array<vec3d>@+ pt_arr, array<double>@+ u_arr, array<double>@+ w_arr )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AddFitModelTargetPtsFixedUWs ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelFitModelTargetPt( int index )", VSP_FN( vsp::DelFitModelTargetPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllFitModelTargetPts()", VSP_FN( vsp::DelAllFitModelTargetPts ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SortFitModelTargetPtsByDist()", VSP_FN( vsp::SortFitModelTargetPtsByDist ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int MoveFitModelTargetPt( int index, int reorder_type )", VSP_FN( vsp::MoveFitModelTargetPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumFitModelTargetPts()", VSP_FN( vsp::GetNumFitModelTargetPts ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d GetFitModelTargetPt( int index )", VSP_FN( vsp::GetFitModelTargetPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPt( int index, const vec3d & in pt )", VSP_FN( vsp::SetFitModelTargetPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFitModelTargetPtGeom( int index )", VSP_FN( vsp::GetFitModelTargetPtGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPtGeom( int index, const string & in geom_id )", VSP_FN( vsp::SetFitModelTargetPtGeom ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetFitModelTargetPtSurfIndx( int index )", VSP_FN( vsp::GetFitModelTargetPtSurfIndx ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPtSurfIndx( int index, int surf_indx )", VSP_FN( vsp::SetFitModelTargetPtSurfIndx ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double GetFitModelTargetPtU( int index )", VSP_FN( vsp::GetFitModelTargetPtU ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double GetFitModelTargetPtDist( int index )", VSP_FN( vsp::GetFitModelTargetPtDist ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double GetFitModelTargetPtW( int index )", VSP_FN( vsp::GetFitModelTargetPtW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPtUW( int index, double u, double w )", VSP_FN( vsp::SetFitModelTargetPtUW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetFitModelTargetPtUType( int index )", VSP_FN( vsp::GetFitModelTargetPtUType ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPtUType( int index, int u_type )", VSP_FN( vsp::SetFitModelTargetPtUType ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetFitModelTargetPtWType( int index )", VSP_FN( vsp::GetFitModelTargetPtWType ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFitModelTargetPtWType( int index, int w_type )", VSP_FN( vsp::SetFitModelTargetPtWType ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d GetFitModelTargetPtSurfPt( int index )", VSP_FN( vsp::GetFitModelTargetPtSurfPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFitModelVar( const string & in parm_id )", VSP_FN( vsp::AddFitModelVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelFitModelVar( const string & in parm_id )", VSP_FN( vsp::DelFitModelVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllFitModelVars()", VSP_FN( vsp::DelAllFitModelVars ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumFitModelVars()", VSP_FN( vsp::GetNumFitModelVars ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFitModelVar( int index )", VSP_FN( vsp::GetFitModelVar ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFitModelVarVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFitModelVarVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SearchFitModelTargetUW()", VSP_FN( vsp::SearchFitModelTargetUW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RefineFitModelTargetUW()", VSP_FN( vsp::RefineFitModelTargetUW ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double UpdateFitModelDist()", VSP_FN( vsp::UpdateFitModelDist ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double GetFitModelDist()", VSP_FN( vsp::GetFitModelDist ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumFitModelOptVars()", VSP_FN( vsp::GetNumFitModelOptVars ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int OptimizeFitModel()", VSP_FN( vsp::OptimizeFitModel ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool CanUndoFitModel()", VSP_FN( vsp::CanUndoFitModel ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "bool UndoFitModel()", VSP_FN( vsp::UndoFitModel ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SaveFitModelFile( const string & in file_name )", VSP_FN( vsp::SaveFitModelFile ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int LoadFitModelFile( const string & in file_name )", VSP_FN( vsp::LoadFitModelFile ) );
    assert( r >= 0 );

    //=== Register Var Preset Functions ====//

    r = se->RegisterGlobalFunction( "string AddVarPresetGroup( const string & in group_name )", VSP_FN( vsp::AddVarPresetGroup ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddVarPresetSetting( const string & in group_id, const string & in setting_name )", VSP_FN( vsp::AddVarPresetSetting ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddVarPresetParm( const string & in group_id, const string & in parm_ID )", VSP_FN( vsp::AddVarPresetParm ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllVarPresetGroups()", VSP_FN( vsp::DeleteAllVarPresetGroups ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllVarPresetSettings( const string & in group_id )", VSP_FN( vsp::DeleteAllVarPresetSettings ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteVarPresetGroup( const string & in group_id )", VSP_FN( vsp::DeleteVarPresetGroup ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteVarPresetSetting( const string & in group_id, const string & in setting_id )", VSP_FN( vsp::DeleteVarPresetSetting ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteVarPresetParm( const string & in group_id, const string & in parm_id )", VSP_FN( vsp::DeleteVarPresetParm ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetVarPresetParmVal( const string & in group_id, const string & in setting_id, const string & in parm_id, double parm_val )", VSP_FN( vsp::SetVarPresetParmVal ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetVarPresetParmVal( const string & in group_id, const string & in setting_id, const string & in parm_id )", VSP_FN( vsp::GetVarPresetParmVal ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetGroupName( const string & in group_id )", VSP_FN( vsp::GetGroupName ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetSettingName( const string & in setting_id )", VSP_FN( vsp::GetSettingName ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetGroupName( const string & in group_id, const string & in group_name )", VSP_FN( vsp::SetGroupName ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetSettingName( const string & in setting_id, const string & in setting_name )", VSP_FN( vsp::SetSettingName ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetVarPresetGroups()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVarPresetGroups ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetVarPresetSettings( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVarPresetSettings ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetVarPresetParmIDs( const string & in group_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVarPresetParmIDs ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<double>@+ GetVarPresetParmVals( const string & in setting_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetVarPresetParmVals ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetVarPresetParmVals( const string& in setting_id, array<double>@+ parm_vals )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetVarPresetParmVals ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SaveVarPresetParmVals( const string & in group_id, const string & in setting_id )", VSP_FN( vsp::SaveVarPresetParmVals ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ApplyVarPresetSetting( const string & in group_id, const string & in setting_id )", VSP_FN( vsp::ApplyVarPresetSetting ));
    assert( r >= 0 );


    //=== Register Mode Functions ====//

    r = se->RegisterGlobalFunction( "string CreateAndAddMode( const string & in name, int normal_set, int degen_set )", VSP_FN( vsp::CreateAndAddMode ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetNumModes()", VSP_FN( vsp::GetNumModes ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllModes()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllModes ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelMode( const string & in mode_id )", VSP_FN( vsp::DelMode ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelAllModes()", VSP_FN( vsp::DelAllModes ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetModeName( const string & in mid )", VSP_FN( vsp::GetModeName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetModeName( const string & in mid, const string & in name )", VSP_FN( vsp::SetModeName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ApplyModeSettings( const string & in mode_id )", VSP_FN( vsp::ApplyModeSettings ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowOnlyMode( const string & in mode_id )", VSP_FN( vsp::ShowOnlyMode ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ModeAddGroupSetting( const string & in mode_id, const string & in group_id, const string & in setting_id )", VSP_FN( vsp::ModeAddGroupSetting ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string ModeGetGroup( const string & in mode_id, int indx )", VSP_FN( vsp::ModeGetGroup ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string ModeGetSetting( const string & in mode_id, int indx )", VSP_FN( vsp::ModeGetSetting ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string ModeGetGroupSetting( const string & in mode_id, const string & in gid )", VSP_FN( vsp::ModeGetGroupSetting ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ ModeGetAllGroups( const string & in mode_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ModeGetAllGroups ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ ModeGetAllSettings( const string & in mode_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ModeGetAllSettings ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RemoveGroupSetting( const string & in mode_id, int indx )", VSP_FN( vsp::RemoveGroupSetting ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RemoveGroupSettingByID( const string & in mode_id, const string & in gid )", VSP_FN( vsp::RemoveGroupSettingByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void RemoveAllGroupSettings( const string & in mode_id )", VSP_FN( vsp::RemoveAllGroupSettings ));
    assert( r >= 0 );

    //=== Register PCurve Functions ====//

    r = se->RegisterGlobalFunction( "void SetPCurve( const string& in geom_id, const int & in pcurveid, array<double>@+ tvec, array<double>@+ valvec, const int & in newtype )", VSP_MFN_GLOBAL( ScriptMgrSingleton, SetPCurve ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void PCurveConvertTo( const string & in geom_id, const int & in pcurveid, const int & in newtype )", VSP_FN( vsp::PCurveConvertTo ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "int PCurveGetType( const string & in geom_id, const int & in pcurveid )", VSP_FN( vsp::PCurveGetType ) );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "array<double>@+ PCurveGetTVec( const string & in geom_id, const int & in pcurveid )", VSP_MFN_GLOBAL( ScriptMgrSingleton, PCurveGetTVec ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "array<double>@+ PCurveGetValVec( const string & in geom_id, const int & in pcurveid )", VSP_MFN_GLOBAL( ScriptMgrSingleton, PCurveGetValVec ), &ScriptMgr );
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void PCurveDeletePt( const string & in geom_id, const int & in pcurveid, const int & in indx )", VSP_FN( vsp::PCurveDeletePt ));
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "int PCurveSplit( const string & in geom_id, const int & in pcurveid, const double & in tsplit )", VSP_FN( vsp::PCurveSplit ));
    assert( r >= 0 ); // TODO: Example


    r = se->RegisterGlobalFunction( "void ApproximateAllPropellerPCurves( const string & in geom_id )", VSP_FN( vsp::ApproximateAllPropellerPCurves ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ResetPropellerThicknessCurve( const string & in geom_id )", VSP_FN( vsp::ResetPropellerThicknessCurve ));
    assert( r >= 0 );

    //=== Register ParasiteDragTool Functions ====//

    r = se->RegisterGlobalFunction( "void AddExcrescence(const string & in excresName, const int & in excresType, const double & in excresVal)", VSP_FN( vsp::AddExcrescence ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetNumExcrescences()", VSP_FN( vsp::GetNumExcrescences ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllExcrescences()", VSP_FN( vsp::DeleteAllExcrescences ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteExcrescence(const int & in excresName)", VSP_FN( vsp::DeleteExcrescence ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void UpdateParasiteDrag()", VSP_FN( vsp::UpdateParasiteDrag ) );
    assert( r >= 0 ); // TODO: Example

    r = se->RegisterGlobalFunction( "string ExportParasiteDragToCSV( const string & in file_name )", VSP_FN( vsp::ExportParasiteDragToCSV ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParasiteDragRefWingID( const string & in geom_id )", VSP_FN( vsp::SetParasiteDragRefWingID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetParasiteDragRefWingID()", VSP_FN( vsp::GetParasiteDragRefWingID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetParasiteDragModeID( const string & in mode_id )", VSP_FN( vsp::SetParasiteDragModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetParasiteDragModeID()", VSP_FN( vsp::GetParasiteDragModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetWaveDragRefWingID( const string & in geom_id )", VSP_FN( vsp::SetWaveDragRefWingID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetWaveDragRefWingID()", VSP_FN( vsp::GetWaveDragRefWingID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetWaveDragModeID( const string & in mode_id )", VSP_FN( vsp::SetWaveDragModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetWaveDragModeID()", VSP_FN( vsp::GetWaveDragModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetVSPAEROModeID( const string & in mode_id )", VSP_FN( vsp::SetVSPAEROModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetVSPAEROModeID()", VSP_FN( vsp::GetVSPAEROModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetVSPAEROCGModeID( const string & in mode_id )", VSP_FN( vsp::SetVSPAEROCGModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetVSPAEROCGModeID()", VSP_FN( vsp::GetVSPAEROCGModeID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddWaveDragFlowSubSurf( const string & in ss_id )", VSP_FN( vsp::AddWaveDragFlowSubSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteWaveDragFlowSubSurf( const string & in ss_id )", VSP_FN( vsp::DeleteWaveDragFlowSubSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetWaveDragFlowSubSurfVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetWaveDragFlowSubSurfVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteAtmosphereCSVFile( const string & in file_name, const int & in atmos_type )", VSP_FN( vsp::WriteAtmosphereCSVFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CalcAtmosphere( const double & in alt, const double & in delta_temp, const int & in atmos_type, double & out temp, double & out pres, double & out pres_ratio, double & out rho_ratio )", VSP_FN( vsp::CalcAtmosphere ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteBodyFFCSVFile( const string & in file_name )", VSP_FN( vsp::WriteBodyFFCSVFile ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void WriteWingFFCSVFile( const string & in file_name )", VSP_FN( vsp::WriteWingFFCSVFile ) );
    assert( r >= 0 );

     // TODO: Improve description
    r = se->RegisterGlobalFunction( "void WriteCfEqnCSVFile( const string & in file_name )", VSP_FN( vsp::WriteCfEqnCSVFile ) );
    assert( r >= 0 );

     // TODO: Improve description
    r = se->RegisterGlobalFunction( "void WritePartialCfMethodCSVFile( const string & in file_name )", VSP_FN( vsp::WritePartialCfMethodCSVFile ) );
    assert( r >= 0 );

    //=== Register Surface Query Functions ===//

    r = se->RegisterGlobalFunction( "double CompareGeomSurfaces( const string & in geom_a, const string & in geom_b )", VSP_FN( vsp::CompareGeomSurfaces ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "vec3d CompPnt01( const string & in geom_id, const int & in surf_indx, const double & in u, const double & in w )", VSP_FN(vsp::CompPnt01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d CompNorm01( const string & in geom_id, const int & in surf_indx, const double & in u, const double & in w )", VSP_FN(vsp::CompNorm01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d CompTanU01( const string & in geom_id, const int & in surf_indx, const double & in u, const double & in w )", VSP_FN(vsp::CompTanU01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d CompTanW01( const string & in geom_id, const int & in surf_indx, const double & in u, const double & in w )", VSP_FN(vsp::CompTanW01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CompCurvature01( const string & in geom_id, const int & in surf_indx, const double & in u, const double & in w, double & out k1, double & out k2, double & out ka, double & out kg )", VSP_FN(vsp::CompCurvature01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double ProjPnt01( const string & in geom_id, const int & in surf_indx, const vec3d & in pt, double & out u, double & out w )", VSP_FN(vsp::ProjPnt01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double ProjPnt01I( const string & in geom_id, const vec3d & in pt, int & out surf_indx, double & out u, double & out w )", VSP_FN(vsp::ProjPnt01I) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double ProjPnt01Guess( const string & in geom_id, const int & in surf_indx, const vec3d & in pt, const double & in u0, const double & in w0, double & out u, double & out w )", VSP_FN(vsp::ProjPnt01Guess) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double AxisProjPnt01( const string & in geom_id, const int & in surf_indx, const int & in iaxis, const vec3d & in pt, double & out u_out, double & out w_out )", VSP_FN(vsp::AxisProjPnt01) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double AxisProjPnt01I( const string & in geom_id, const int & in iaxis, const vec3d & in pt, int & out surf_indx_out, double & out u_out, double & out w_out )", VSP_FN(vsp::AxisProjPnt01I) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double AxisProjPnt01Guess( const string & in geom_id, const int & in surf_indx, const int & in iaxis, const vec3d & in pt, const double & in u0, const double & in w0, double & out u_out, double & out w_out )", VSP_FN(vsp::AxisProjPnt01Guess) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool InsideSurf( const string & in geom_id, const int & in surf_indx, const vec3d & in pt )", VSP_FN(vsp::InsideSurf) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double FindRST( const string & in geom_id, const int & in surf_indx, const vec3d & in pt, double & out r, double & out s, double & out t )", VSP_FN(vsp::FindRST) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double FindRSTGuess( const string & in geom_id, const int & in surf_indx, const vec3d & in pt, const double & in r0, const double & in s0, const double & in t0, double & out r, double & out s, double & out t )", VSP_FN(vsp::FindRSTGuess) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "vec3d CompPntRST( const string & in geom_id, const int & in surf_indx, const double & in r, const double & in s, const double & in t )", VSP_FN(vsp::CompPntRST) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ CompVecPntRST(const string & in geom_id, const int & in surf_indx, array<double>@+ rs, array<double>@+ ss, array<double>@+ ts )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CompVecPntRST ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertRSTtoLMN( const string & in geom_id, const int & in surf_indx, const double & in r, const double & in s, const double & in t, double & out l_out, double & out m_out, double & out n_out )", VSP_FN(vsp::ConvertRSTtoLMN) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertRtoL( const string & in geom_id, const int & in surf_indx, const double & in r, double & out l_out )", VSP_FN(vsp::ConvertRtoL) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertLMNtoRST( const string & in geom_id, const int & in surf_indx, const double & in l, const double & in m, const double & in n, double & out r_out, double & out s_out, double & out t_out )", VSP_FN(vsp::ConvertLMNtoRST) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertLtoR( const string & in geom_id, const int & in surf_indx, const double & in l, double & out r_out )", VSP_FN(vsp::ConvertLtoR) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertUtoEta( const string & in geom_id, const double & in u, double & out eta )", VSP_FN(vsp::ConvertUtoEta) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertEtatoU( const string & in geom_id, const double & in eta, double & out u_out )", VSP_FN(vsp::ConvertEtatoU) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertRSTtoLMNVec(const string & in geom_id, const int & in surf_indx, array<double>@+ rs, array<double>@+ ss, array<double>@+ ts, array<double>@+ ls, array<double>@+ ms, array<double>@+ ns )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ConvertRSTtoLMNVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ConvertLMNtoRSTVec(const string & in geom_id, const int & in surf_indx, array<double>@+ ls, array<double>@+ ms, array<double>@+ ns, array<double>@+ rs, array<double>@+ ss, array<double>@+ ts )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ConvertLMNtoRSTVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void GetUWTess01(const string & in geom_id, int & in surf_indx, array<double>@+ us, array<double>@+ ws )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetUWTess01 ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ ControlSurfaceHingeLine( const string & in id, const int & in surf_indx )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ControlSurfaceHingeLine ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ CompVecPnt01(const string & in geom_id, const int & in surf_indx, array<double>@+ us, array<double>@+ ws )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CompVecPnt01 ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ CompVecDegenPnt01(const string & in geom_id, const int & in surf_indx, const int & in degen_type, array<double>@+ us, array<double>@+ ws )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CompVecDegenPnt01 ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<vec3d>@+ CompVecNorm01(const string & in geom_id, const int & in surf_indx, array<double>@+ us, array<double>@+ ws )", VSP_MFN_GLOBAL( ScriptMgrSingleton, CompVecNorm01 ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void CompVecCurvature01(const string & in geom_id, const int & in surf_indx, array<double>@+ us, array<double>@+ ws, array<double>@+ k1s, array<double>@+ k2s, array<double>@+ kas, array<double>@+ kgs)", VSP_MFN_GLOBAL( ScriptMgrSingleton, CompVecCurvature01 ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ProjVecPnt01(const string & in geom_id, const int & in surf_indx, array<vec3d>@+ pts, array<double>@+ us, array<double>@+ ws, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ProjVecPnt01 ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ProjVecPnt01Guess(const string & in geom_id, const int & in surf_indx, array<vec3d>@+ pts, array<double>@+ u0s, array<double>@+ w0s, array<double>@+ us, array<double>@+ ws, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, ProjVecPnt01Guess ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AxisProjVecPnt01(const string & in geom_id, const int & in surf_indx, const int & in iaxis, array<vec3d>@+ pts, array<double>@+ us, array<double>@+ ws, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AxisProjVecPnt01 ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AxisProjVecPnt01Guess(const string & in geom_id, int & in surf_indx, const int & in iaxis, array<vec3d>@+ pts, array<double>@+ u0s, array<double>@+ w0s, array<double>@+ us, array<double>@+ ws, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, AxisProjVecPnt01Guess ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<bool>@+ VecInsideSurf( const string & in geom_id, const int & in surf_indx, array<vec3d>@+ pts )", VSP_MFN_GLOBAL( ScriptMgrSingleton, VecInsideSurf ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void FindRSTVec(const string & in geom_id, const int & in surf_indx, array<vec3d>@+ pts, array<double>@+ rs, array<double>@+ ss, array<double>@+ ts, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindRSTVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void FindRSTVecGuess(const string & in geom_id, const int & in surf_indx, array<vec3d>@+ pts, array<double>@+ r0s, array<double>@+ s0s, array<double>@+ t0s, array<double>@+ rs, array<double>@+ ss, array<double>@+ ts, array<double>@+ ds )", VSP_MFN_GLOBAL( ScriptMgrSingleton, FindRSTVecGuess ), &ScriptMgr );
    assert( r >= 0 );

    //=== Register Measure Functions ===//

    r = se->RegisterGlobalFunction( "void ShowAllRulers()", VSP_FN( vsp::ShowAllRulers ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void HideAllRulers()", VSP_FN( vsp::HideAllRulers ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowAllProbes()", VSP_FN( vsp::ShowAllProbes ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void HideAllProbes()", VSP_FN( vsp::HideAllProbes ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowAllProtractors()", VSP_FN( vsp::ShowAllProtractors ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void HideAllProtractors()", VSP_FN( vsp::HideAllProtractors ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ShowAllRSTProbes()", VSP_FN( vsp::ShowAllRSTProbes ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void HideAllRSTProbes()", VSP_FN( vsp::HideAllRSTProbes ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddProtractor( const string & in startgeomid, int startsurfindx, double startu, double startw, const string & in midgeomid, int midsurfindx, double midu, double midw, const string & in endgeomid, int endsurfindx, double endu, double endw, const string & in name )", VSP_FN( vsp::AddProtractor ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllProtractors()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllProtractors ), &ScriptMgr);
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelProtractor( const string & in id )", VSP_FN( vsp::DelProtractor ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllProtractors()", VSP_FN( vsp::DeleteAllProtractors ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetMeasureOriginGeomID( const string & in measure_id, const string & in geom_id )", VSP_FN( vsp::SetMeasureOriginGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetMeasureOriginGeomID( const string & in measure_id )", VSP_FN( vsp::GetMeasureOriginGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetMeasureMidGeomID( const string & in measure_id, const string & in geom_id )", VSP_FN( vsp::SetMeasureMidGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetMeasureMidGeomID( const string & in measure_id )", VSP_FN( vsp::GetMeasureMidGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetMeasureEndGeomID( const string & in measure_id, const string & in geom_id )", VSP_FN( vsp::SetMeasureEndGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetMeasureEndGeomID( const string & in measure_id )", VSP_FN( vsp::GetMeasureEndGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddRSTProbe( const string & in geomid, int surfindx, double r, double s, double t, const string & in name )", VSP_FN( vsp::AddRSTProbe ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetAllRSTProbes()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllRSTProbes ), &ScriptMgr);
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelRSTProbe( const string & in id )", VSP_FN( vsp::DelRSTProbe ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteAllRSTProbes()", VSP_FN( vsp::DeleteAllRSTProbes ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddRuler( const string & in startgeomid, int startsurfindx, double startu, double startw, const string & in endgeomid, int endsurfindx, double endu, double endw, const string & in name )", VSP_FN( vsp::AddRuler ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllRulers()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllRulers ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelRuler( const string & in id )", VSP_FN( vsp::DelRuler ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllRulers()", VSP_FN( vsp::DeleteAllRulers ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddProbe( const string & in geomid, int surfindx, double u, double w, const string & in name )", VSP_FN( vsp::AddProbe ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllProbes()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllProbes ), &ScriptMgr);
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelProbe( const string & in id )", VSP_FN( vsp::DelProbe ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteAllProbes()", VSP_FN( vsp::DeleteAllProbes ));
    assert( r >= 0 );

    //=== Register FeaStructure and FEA Mesh Functions ====//

    r = se->RegisterGlobalFunction( "int AddFeaStruct( const string & in geom_id, bool init_skin = true, int surfindex = 0 )", VSP_FN( vsp::AddFeaStruct ) );
    assert( r >= 0 ); // TODO: Force init_skin to true always


    r = se->RegisterGlobalFunction( "void DeleteFeaStruct( const string & in geom_id, int fea_struct_ind )", VSP_FN( vsp::DeleteFeaStruct ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaStructure( const string & in geom_id, bool init_skin = true, int surfindex = 0 )", VSP_FN( vsp::AddFeaStructure ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaStructByID( const string & in struct_id )", VSP_FN( vsp::DeleteFeaStructByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetFeaMeshStructIndex()", VSP_FN( vsp::GetFeaMeshStructIndex ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshStructIndex( int struct_index )", VSP_FN( vsp::SetFeaMeshStructIndex ));
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaMeshStructID()", VSP_FN( vsp::GetFeaMeshStructID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshStructID( const string & in struct_id )", VSP_FN( vsp::SetFeaMeshStructID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaStructID( const string & in geom_id, int fea_struct_ind )", VSP_FN( vsp::GetFeaStructID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetFeaStructIndex( const string & in struct_id )", VSP_FN( vsp::GetFeaStructIndex ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaStructParentGeomID( const string & in struct_id )", VSP_FN( vsp::GetFeaStructParentGeomID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaStructName( const string & in geom_id, int fea_struct_ind )", VSP_FN( vsp::GetFeaStructName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaStructNameByID( const string & in struct_id )", VSP_FN( vsp::GetFeaStructNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetFeaStructName( const string & in geom_id, int fea_struct_ind, const string & in name )", VSP_FN( vsp::SetFeaStructName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaStructNameByID( const string & in struct_id, const string & in name )", VSP_FN( vsp::SetFeaStructNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaStructIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaStructIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaStructIDVecByGeom( const string & in geom_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaStructIDVecByGeom ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetFeaPartName( const string & in part_id, const string & in name )", VSP_FN( vsp::SetFeaPartName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "double GetFeaMeshVal( const string & in geom_id, int fea_struct_ind, int type )", VSP_FN( vsp::GetFeaMeshVal ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double GetFeaMeshValByID( const string & in struct_id, int type )", VSP_FN( vsp::GetFeaMeshValByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshVal( const string & in geom_id, int fea_struct_ind, int type, double val )", VSP_FN( vsp::SetFeaMeshVal ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshValByID( const string & in struct_id, int type, double val )", VSP_FN( vsp::SetFeaMeshValByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaMeshFileName( const string & in geom_id, int fea_struct_ind, int file_type )", VSP_FN( vsp::GetFeaMeshFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaMeshFileNameByID( const string & in struct_id, int file_type )", VSP_FN( vsp::GetFeaMeshFileNameByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshFileName( const string & in geom_id, int fea_struct_ind, int file_type, const string & in file_name )", VSP_FN( vsp::SetFeaMeshFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaMeshFileNameByID( const string & in struct_id, int file_type, const string & in file_name )", VSP_FN( vsp::SetFeaMeshFileNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ComputeFeaMesh( const string & in geom_id, int fea_struct_ind, int file_type )", VSP_FN_PR( vsp::ComputeFeaMesh, ( const string & in, int, int ), void ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void ComputeFeaMesh( const string & in struct_id, int file_type )", VSP_FN_PR( vsp::ComputeFeaMesh, ( const string & in, int ), void ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddFeaPart( const string & in geom_id, int fea_struct_ind, int type )", VSP_FN( vsp::AddFeaPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaPartByID( const string & in struct_id, int type )", VSP_FN( vsp::AddFeaPartByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteFeaPart( const string & in geom_id, int fea_struct_ind, const string & in part_id )", VSP_FN( vsp::DeleteFeaPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaPartByID( const string & in part_id )", VSP_FN( vsp::DeleteFeaPartByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaPartID( const string & in fea_struct_id, int fea_part_index )", VSP_FN( vsp::GetFeaPartID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaPartName( const string & in part_id )", VSP_FN( vsp::GetFeaPartName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetFeaPartType( const string & in part_id )", VSP_FN( vsp::GetFeaPartType ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetFeaSubSurfIndex( const string & in ss_id )", VSP_FN( vsp::GetFeaSubSurfIndex ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int GetFeaPolySparNumPt( const string & in pspar_id )", VSP_FN( vsp::GetFeaPolySparNumPt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddFeaPolySparPt( const string & in pspar_id )", VSP_FN( vsp::AddFeaPolySparPt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string InsertFeaPolySparPt( const string & in pspar_id, int index )", VSP_FN( vsp::InsertFeaPolySparPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string InsertFeaPolySparPtByID( const string & in pspar_id, const string & in before_pt_id )", VSP_FN( vsp::InsertFeaPolySparPtByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelFeaPolySparPt( const string & in pspar_id, int index )", VSP_FN( vsp::DelFeaPolySparPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DelFeaPolySparPtByID( const string & in pspar_id, const string & in pt_id )", VSP_FN( vsp::DelFeaPolySparPtByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelAllFeaPolySparPt( const string & in pspar_id )", VSP_FN( vsp::DelAllFeaPolySparPt ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int MoveFeaPolySparPt( const string & in pspar_id, int index, int reorder_type )", VSP_FN( vsp::MoveFeaPolySparPt ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int MoveFeaPolySparPtByID( const string & in pspar_id, const string & in pt_id, int reorder_type )", VSP_FN( vsp::MoveFeaPolySparPtByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetFeaPolySparPtName( const string & in pspar_id, int index, const string & in name )", VSP_FN( vsp::SetFeaPolySparPtName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPolySparPtNameByID( const string & in pspar_id, const string & in pt_id, const string & in name )", VSP_FN( vsp::SetFeaPolySparPtNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaPolySparPtName( const string & in pspar_id, int index )", VSP_FN( vsp::GetFeaPolySparPtName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPolySparPtNameByID( const string & in pspar_id, const string & in pt_id )", VSP_FN( vsp::GetFeaPolySparPtNameByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaPolySparPtID( const string & in pspar_id, int index )", VSP_FN( vsp::GetFeaPolySparPtID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetAllFeaPolySparPtIDVec( const string & in pspar_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetAllFeaPolySparPtIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int NumFeaStructures()", VSP_FN( vsp::NumFeaStructures ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int NumFeaParts( const string & in fea_struct_id )", VSP_FN( vsp::NumFeaParts ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int NumFeaSubSurfs( const string & in fea_struct_id )", VSP_FN( vsp::NumFeaSubSurfs ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaPartIDVec(const string & in fea_struct_id)", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaPartIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaSubSurfIDVec(const string & in fea_struct_id)", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaSubSurfIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetFeaPartPerpendicularSparID( const string & in part_id, const string & in perpendicular_spar_id )", VSP_FN( vsp::SetFeaPartPerpendicularSparID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaPartPerpendicularSparID( const string & in part_id )", VSP_FN( vsp::GetFeaPartPerpendicularSparID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPartStartWingSectionID( const string & in part_id, const string & in xsec_id )", VSP_FN( vsp::SetFeaPartStartWingSectionID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPartStartWingSectionID( const string & in part_id )", VSP_FN( vsp::GetFeaPartStartWingSectionID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPartEndWingSectionID( const string & in part_id, const string & in xsec_id )", VSP_FN( vsp::SetFeaPartEndWingSectionID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPartEndWingSectionID( const string & in part_id )", VSP_FN( vsp::GetFeaPartEndWingSectionID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPartPropertyID( const string & in part_id, const string & in property_id )", VSP_FN( vsp::SetFeaPartPropertyID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPartPropertyID( const string & in part_id )", VSP_FN( vsp::GetFeaPartPropertyID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPartCapPropertyID( const string & in part_id, const string & in property_id )", VSP_FN( vsp::SetFeaPartCapPropertyID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPartCapPropertyID( const string & in part_id )", VSP_FN( vsp::GetFeaPartCapPropertyID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaFixPointParentID( const string & in fixpt_id, const string & in part_id )", VSP_FN( vsp::SetFeaFixPointParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaFixPointParentID( const string & in fixpt_id )", VSP_FN( vsp::GetFeaFixPointParentID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaFixPointOtherGeomID( const string & in fixpt_id, const string & in geom_id )", VSP_FN( vsp::SetFeaFixPointOtherGeomID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaFixPointOtherGeomID( const string & in fixpt_id )", VSP_FN( vsp::GetFeaFixPointOtherGeomID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void SetFeaSubSurfName( const string & in subsurf_id, const string & in name )", VSP_FN( vsp::SetFeaSubSurfName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetFeaSubSurfName( const string & in subsurf_id )", VSP_FN( vsp::GetFeaSubSurfName ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddFeaSubSurf( const string & in geom_id, int fea_struct_ind, int type )", VSP_FN( vsp::AddFeaSubSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaSubSurfByID( const string & in struct_id, int type )", VSP_FN( vsp::AddFeaSubSurfByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DeleteFeaSubSurf( const string & in geom_id, int fea_struct_ind, const string & in ss_id )", VSP_FN( vsp::DeleteFeaSubSurf ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaSubSurfByID( const string & in ss_id )", VSP_FN( vsp::DeleteFeaSubSurfByID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string AddFeaBC( const string & in fea_struct_id, int type )", VSP_FN( vsp::AddFeaBC ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void DelFeaBC( const string & in fea_struct_id, const string & in ss_id )", VSP_FN( vsp::DelFeaBC ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaBCIDVec(const string & in fea_struct_id)", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaBCIDVec ), &ScriptMgr );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int NumFeaBCs( const string & in fea_struct_id )", VSP_FN( vsp::NumFeaBCs ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaBCPartID( const string & in bc_id, const string & in part_id )", VSP_FN( vsp::SetFeaBCPartID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaBCPartID( const string & in bc_id )", VSP_FN( vsp::GetFeaBCPartID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaBCSubSurfID( const string & in bc_id, const string & in ss_id )", VSP_FN( vsp::SetFeaBCSubSurfID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaBCSubSurfID( const string & in bc_id )", VSP_FN( vsp::GetFeaBCSubSurfID ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void AddFeaTrimPart( const string & in trim_id, const string & in part_id )", VSP_FN( vsp::AddFeaTrimPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaTrimPart( const string & in trim_id, int index )", VSP_FN( vsp::DeleteFeaTrimPart ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaTrimPartByID( const string & in trim_id, const string & in part_id )", VSP_FN( vsp::DeleteFeaTrimPartByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaTrimPartIDVec( const string & in trim_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaTrimPartIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaAssembly()", VSP_FN( vsp::AddFeaAssembly ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaAssembly( const string & in assembly_id )", VSP_FN( vsp::DeleteFeaAssembly ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int NumFeaAssemblies()", VSP_FN( vsp::NumFeaAssemblies ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaAssemblyIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaAssemblyIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaAssemblyName( const string & in assembly_id )", VSP_FN( vsp::GetFeaAssemblyName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaAssemblyName( const string & in assembly_id, const string & in name )", VSP_FN( vsp::SetFeaAssemblyName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFeaStructureToAssembly( const string & in assembly_id, const string & in struct_id )", VSP_FN( vsp::AddFeaStructureToAssembly ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaStructureFromAssembly( const string & in assembly_id, const string & in struct_id )", VSP_FN( vsp::DeleteFeaStructureFromAssembly ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void AddFeaAssemblyStructure( const string & in assembly_id, const string & in struct_id )", VSP_FN( vsp::AddFeaAssemblyStructure ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaAssemblyStructure( const string & in assembly_id, const string & in struct_id )", VSP_FN( vsp::DeleteFeaAssemblyStructure ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaAssemblyStructureIDVec( const string & in assembly_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaAssemblyStructureIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaAssemblyConnection( const string & in assembly_id, const string & in start_fix_pt_id, const string & in start_struct_id, int start_index, const string & in end_fix_pt_id, const string & in end_struct_id, int end_index )", VSP_FN( vsp::AddFeaAssemblyConnection ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaAssemblyConnection( const string & in assembly_id, int connection_index )", VSP_FN( vsp::DeleteFeaAssemblyConnection ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaAssemblyConnectionByID( const string & in assembly_id, const string & in connection_id )", VSP_FN( vsp::DeleteFeaAssemblyConnectionByID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int NumFeaAssemblyConnections( const string & in assembly_id )", VSP_FN( vsp::NumFeaAssemblyConnections ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaAssemblyConnectionIDVec( const string & in assembly_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaAssemblyConnectionIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaAssemblyFileName( const string & in assembly_id, int file_type )", VSP_FN( vsp::GetFeaAssemblyFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaAssemblyFileName( const string & in assembly_id, int file_type, const string & in file_name )", VSP_FN( vsp::SetFeaAssemblyFileName ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void ComputeFeaAssemblyMesh( const string & in assembly_id )", VSP_FN( vsp::ComputeFeaAssemblyMesh ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaLayer( const string & in material_id )", VSP_FN( vsp::AddFeaLayer ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaLayer( const string & in material_id, const string & in layer_id )", VSP_FN( vsp::DeleteFeaLayer ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "int NumFeaLayers( const string & in material_id )", VSP_FN( vsp::NumFeaLayers ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaLayerIDVec( const string & in material_id )", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaLayerIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaLayerMaterialID( const string & in layer_id, const string & in material_id )", VSP_FN( vsp::SetFeaLayerMaterialID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaLayerMaterialID( const string & in layer_id )", VSP_FN( vsp::GetFeaLayerMaterialID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaMaterialIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaMaterialIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaMaterial( const string & in id )", VSP_FN( vsp::DeleteFeaMaterial ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaMaterial()", VSP_FN( vsp::AddFeaMaterial ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "array<string>@+ GetFeaPropertyIDVec()", VSP_MFN_GLOBAL( ScriptMgrSingleton, GetFeaPropertyIDVec ), &ScriptMgr );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void DeleteFeaProperty( const string & in id )", VSP_FN( vsp::DeleteFeaProperty ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string AddFeaProperty( int property_type = 0 )", VSP_FN( vsp::AddFeaProperty ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void SetFeaPropertyMaterialID( const string & in property_id, const string & in material_id )", VSP_FN( vsp::SetFeaPropertyMaterialID ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "string GetFeaPropertyMaterialID( const string & in property_id )", VSP_FN( vsp::GetFeaPropertyMaterialID ) );
    assert( r >= 0 );
}

void ScriptMgrSingleton::RegisterUtility( asIScriptEngine* se )
{
    int r;
    //==== Register Utility Functions ====//

    // Registered straight onto the API functions.  These used to be ScriptMgrSingleton methods, so
    // they existed for AngelScript alone -- undocumented, and unavailable to C++ or Python callers.
    r = se->RegisterGlobalFunction( "void Print(const string & in data, bool new_line = true )", VSP_FN_PR( vsp::Print, ( const string &, bool ), void ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void Print(const vec3d & in data, bool new_line = true )", VSP_FN_PR( vsp::Print, ( const vec3d &, bool ), void ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void Print(double data, bool new_line = true )", VSP_FN_PR( vsp::Print, ( double, bool ), void ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "void Print(int data, bool new_line = true )", VSP_FN_PR( vsp::Print, ( int, bool ), void ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double Min( double x, double y)", VSP_FN_PR( vsp::Min, ( double, double ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double Max( double x, double y)", VSP_FN_PR( vsp::Max, ( double, double ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double Rad2Deg( double r )", VSP_FN_PR( vsp::Rad2Deg, ( double ), double ) );
    assert( r >= 0 );

    r = se->RegisterGlobalFunction( "double Deg2Rad( double d )", VSP_FN_PR( vsp::Deg2Rad, ( double ), double ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPVersion( )", VSP_FN( vsp::GetVSPVersion ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetVSPVersionMajor( )", VSP_FN( vsp::GetVSPVersionMajor ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetVSPVersionMinor( )", VSP_FN( vsp::GetVSPVersionMinor ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "int GetVSPVersionChange( )", VSP_FN( vsp::GetVSPVersionChange ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPExePath()", VSP_FN( vsp::GetVSPExePath ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool SetVSPAEROPath( const string & in path )", VSP_FN( vsp::SetVSPAEROPath ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPAEROPath()", VSP_FN( vsp::GetVSPAEROPath ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool CheckForVSPAERO( const string & in path )", VSP_FN( vsp::CheckForVSPAERO ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool SetVSPHelpPath( const string & in path )", VSP_FN( vsp::SetVSPHelpPath ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "string GetVSPHelpPath()", VSP_FN( vsp::GetVSPHelpPath ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "bool CheckForVSPHelp( const string & in path )", VSP_FN( vsp::CheckForVSPHelp ) );
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void VSPCheckSetup()", VSP_FN( vsp::VSPCheckSetup ));
    assert( r >= 0 );


    r = se->RegisterGlobalFunction( "void VSPRenew()", VSP_FN( vsp::VSPRenew ));
    assert( r >= 0 );

    //====  Register Proxy Utility Functions ====//

    // GetProxyVec3dArray is deliberately not registered.  The 39 wrappers that return an array of
    // points fill m_ProxyVec3dArray and then call it to convert; called from a script it handed
    // back whatever the last of them left in the buffer, which has no meaning.  It stays as
    // internal plumbing.

}

//===== Utility Functions Vec3d Proxy Array =====//
CScriptArray* ScriptMgrSingleton::GetProxyVec3dArray()
{
    //==== This Will Get Deleted By The Script Engine ====//
    CScriptArray* sarr = CScriptArray::Create( m_Vec3dArrayType, m_ProxyVec3dArray.size() );
    for ( int i = 0 ; i < ( int )sarr->GetSize() ; i++ )
    {
        sarr->SetValue( i, &m_ProxyVec3dArray[i] );
    }
    return sarr;
}

//==== Utility Functions String Proxy Array =====//
CScriptArray* ScriptMgrSingleton::GetProxyStringArray()
{
    //==== This Will Get Deleted By The Script Engine ====//
    CScriptArray* sarr = CScriptArray::Create( m_StringArrayType, m_ProxyStringArray.size() );
    for ( int i = 0 ; i < ( int )sarr->GetSize() ; i++ )
    {
        sarr->SetValue( i, &m_ProxyStringArray[i] );
    }
    return sarr;
}

//==== Utility Functions String Proxy Array =====//
CScriptArray* ScriptMgrSingleton::GetProxyIntArray()
{
    //==== This Will Get Deleted By The Script Engine ====//
    CScriptArray* sarr = CScriptArray::Create( m_IntArrayType, m_ProxyIntArray.size() );
    for ( int i = 0 ; i < ( int )sarr->GetSize() ; i++ )
    {
        sarr->SetValue( i, &m_ProxyIntArray[i] );
    }
    return sarr;
}

//==== Utility Functions String Proxy Array =====//
CScriptArray* ScriptMgrSingleton::GetProxyDoubleArray()
{
    //==== This Will Get Deleted By The Script Engine ====//
    CScriptArray* sarr = CScriptArray::Create( m_DoubleArrayType, m_ProxyDoubleArray.size() );
    for ( int i = 0 ; i < ( int )sarr->GetSize() ; i++ )
    {
        sarr->SetValue( i, &m_ProxyDoubleArray[i] );
    }
    return sarr;
}

CScriptArray* ScriptMgrSingleton::GetProxyIntMatArray()
{
    CScriptArray* sarr = CScriptArray::Create( m_IntMatArrayType, m_ProxyIntMatArray.size() );
    for ( int i = 0; i < ( int )sarr->GetSize(); i++ )
    {
        CScriptArray* darr = CScriptArray::Create( m_IntArrayType, m_ProxyIntMatArray[i].size() );
        for ( int j = 0; j < ( int )darr->GetSize(); j++ )
        {
            darr->SetValue( j, &m_ProxyIntMatArray[i][j]);
        }
        sarr->SetValue( i, darr );
    }
    return sarr;
}

CScriptArray* ScriptMgrSingleton::GetProxyDoubleMatArray()
{
    CScriptArray* sarr = CScriptArray::Create( m_DoubleMatArrayType, m_ProxyDoubleMatArray.size() );
    for ( int i = 0; i < ( int )sarr->GetSize(); i++ )
    {
        CScriptArray* darr = CScriptArray::Create( m_DoubleArrayType, m_ProxyDoubleMatArray[i].size() );
        for ( int j = 0; j < ( int )darr->GetSize(); j++ )
        {
            darr->SetValue( j, &m_ProxyDoubleMatArray[i][j]);
        }
        sarr->SetValue( i, darr );
    }
    return sarr;
}

template < class T >
void ScriptMgrSingleton::FillASArray( vector < T > & in, CScriptArray* out )
{
    out->Resize( in.size() );
    for ( int i = 0 ; i < ( int )in.size() ; i++ )
    {
        out->SetValue( i, &in[i] );
    }
}

template < class T >
void ScriptMgrSingleton::FillSTLVector( CScriptArray* in, vector < T > & out )
{
    out.resize( in->GetSize() );
    for ( int i = 0 ; i < ( int )in->GetSize() ; i++ )
    {
        out[i] = * ( T* ) ( in->At( i ) );
    }
}

// maybe don't template this fill matrix- do a FillDoubleMatrix and FillIntMatrix?
template < class T >
void ScriptMgrSingleton::FillSTLMatrix( CScriptArray* in, vector < vector < T > > & out )
{
    out.resize( in->GetSize() );
    for ( int i = 0 ; i < ( int )in->GetSize() ; i++ )
    {
        CScriptArray* row = ( CScriptArray* ) ( in->At( i ) );
        if ( row )
        {
            FillSTLVector( row, out[i] );
        }
    }
}

//==== Wrappers For API Functions That Return Vectors ====//
CScriptArray* ScriptMgrSingleton::GetGeomTypes()
{
    m_ProxyStringArray = vsp::GetGeomTypes();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::PasteGeomClipboard( const string & parent )
{
    m_ProxyStringArray = vsp::PasteGeomClipboard( parent );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindGeoms()
{
    m_ProxyStringArray = vsp::FindGeoms();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindGeomsWithName( const string & name )
{
    m_ProxyStringArray = vsp::FindGeomsWithName( name );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeomParmIDs( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetGeomParmIDs( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeomChildren( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetGeomChildren( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetSubSurfIDVec( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetSubSurfIDVec( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllSubSurfIDs()
{
    m_ProxyStringArray = vsp::GetAllSubSurfIDs();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetSubSurf( const string & geom_id, const string & name )
{
    m_ProxyStringArray = vsp::GetSubSurf( geom_id, name );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetSubSurfParmIDs( const string & sub_id )
{
    m_ProxyStringArray = vsp::GetSubSurfParmIDs( sub_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetActiveCSNameVec( int CSGroupIndex )
{
    m_ProxyStringArray = vsp::GetActiveCSNameVec( CSGroupIndex );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetActiveCSNameVecByID( const string & group_id )
{
    m_ProxyStringArray = vsp::GetActiveCSNameVecByID( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetActiveCSSubSurfIDVec( const string & group_id )
{
    m_ProxyStringArray = vsp::GetActiveCSSubSurfIDVec( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetActiveCSReflectVec( const string & group_id )
{
    m_ProxyIntArray = vsp::GetActiveCSReflectVec( group_id );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetCompleteCSNameVec( )
{
    m_ProxyStringArray = vsp::GetCompleteCSNameVec( );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetCompleteCSSubSurfIDVec()
{
    m_ProxyStringArray = vsp::GetCompleteCSSubSurfIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetCompleteCSReflectVec()
{
    m_ProxyIntArray = vsp::GetCompleteCSReflectVec();
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetControlSurfaceGroupIDVec()
{
    m_ProxyStringArray = vsp::GetControlSurfaceGroupIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetActuatorDiskIDVec()
{
    m_ProxyStringArray = vsp::GetActuatorDiskIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetUnsteadyGroupIDVec()
{
    m_ProxyStringArray = vsp::GetUnsteadyGroupIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetCpSliceIDVec()
{
    m_ProxyStringArray = vsp::GetCpSliceIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetDesignVarVec()
{
    m_ProxyStringArray = vsp::GetDesignVarVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeometryAnalysisAzimuthParmVec( const string & geom_analysis_id )
{
    m_ProxyStringArray = vsp::GetGeometryAnalysisAzimuthParmVec( geom_analysis_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeometryAnalysisElevationParmVec( const string & geom_analysis_id )
{
    m_ProxyStringArray = vsp::GetGeometryAnalysisElevationParmVec( geom_analysis_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeometryAnalysisCutoutVec( const string & geom_analysis_id )
{
    m_ProxyStringArray = vsp::GetGeometryAnalysisCutoutVec( geom_analysis_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetWaveDragFlowSubSurfVec()
{
    m_ProxyStringArray = vsp::GetWaveDragFlowSubSurfVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetLightIDVec()
{
    m_ProxyStringArray = vsp::GetLightIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllParmLinks()
{
    m_ProxyStringArray = vsp::GetAllParmLinks();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindResultsIDs( const string & name )
{
    m_ProxyStringArray = vsp::FindResultsIDs( name );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllResultsIDs()
{
    m_ProxyStringArray = vsp::GetAllResultsIDs();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeIDVec( const string & collID, const string & attributeName )
{
    m_ProxyStringArray = vsp::GetAttributeIDVec( collID, attributeName );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAvailableCSNameVec( int CSGroupIndex )
{
    m_ProxyStringArray = vsp::GetAvailableCSNameVec( CSGroupIndex );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAvailableCSNameVecByID( const string & group_id )
{
    m_ProxyStringArray = vsp::GetAvailableCSNameVecByID( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAvailableCSSubSurfIDVec( const string & group_id )
{
    m_ProxyStringArray = vsp::GetAvailableCSSubSurfIDVec( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAvailableCSReflectVec( const string & group_id )
{
    m_ProxyIntArray = vsp::GetAvailableCSReflectVec( group_id );
    return GetProxyIntArray();
}

void ScriptMgrSingleton::AddSelectedToCSGroup( CScriptArray* selected, int CSGroupIndex )
{
    vector < int > int_vec;
    FillSTLVector( selected, int_vec );

    vsp::AddSelectedToCSGroup( int_vec, CSGroupIndex );
}

void ScriptMgrSingleton::RemoveSelectedFromCSGroup( CScriptArray* selected, int CSGroupIndex )
{
    vector < int > int_vec;
    FillSTLVector( selected, int_vec );

    vsp::RemoveSelectedFromCSGroup( int_vec, CSGroupIndex );
}

CScriptArray* ScriptMgrSingleton::GetUnsteadyGroupCompIDs( int group_index )
{
    m_ProxyStringArray = vsp::GetUnsteadyGroupCompIDs( group_index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetUnsteadyGroupCompIDsByID( const string & group_id )
{
    m_ProxyStringArray = vsp::GetUnsteadyGroupCompIDsByID( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecTanAngles( const string& xsec_id, int side )
{
    m_ProxyDoubleArray = vsp::GetXSecTanAngles( xsec_id, side );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecTanSlews( const string& xsec_id, int side )
{
    m_ProxyDoubleArray = vsp::GetXSecTanSlews( xsec_id, side );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecTanStrengths( const string& xsec_id, int side )
{
    m_ProxyDoubleArray = vsp::GetXSecTanStrengths( xsec_id, side );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecCurvatures( const string& xsec_id, int side )
{
    m_ProxyDoubleArray = vsp::GetXSecCurvatures( xsec_id, side );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetDriverGroup( const string & geom_id, int section_index )
{
    m_ProxyIntArray = vsp::GetDriverGroup( geom_id, section_index );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetCFDSourceIDVec( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetCFDSourceIDVec( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetSkinSpineIDVec( const string & xsec_id )
{
    m_ProxyStringArray = vsp::GetSkinSpineIDVec( xsec_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecIDVec( const string & xsec_surf_id )
{
    m_ProxyStringArray = vsp::GetXSecIDVec( xsec_surf_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecSurfIDVec( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetXSecSurfIDVec( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetDriverGroupByID( const string & xsec_id )
{
    m_ProxyIntArray = vsp::GetDriverGroupByID( xsec_id );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetUnsteadyGroupSurfIndexes( int group_index )
{
    m_ProxyIntArray = vsp::GetUnsteadyGroupSurfIndexes( group_index );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetUnsteadyGroupSurfIndexesByID( const string & group_id )
{
    m_ProxyIntArray = vsp::GetUnsteadyGroupSurfIndexesByID( group_id );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecParmIDs( const string & xsec_id )
{
    m_ProxyStringArray = vsp::GetXSecParmIDs( xsec_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetXSecPnts( const string& xsec_id )
{
    m_ProxyVec3dArray = vsp::GetXSecPnts( xsec_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::ReadFileXSec( const string& xsec_id, const string& file_name )
{
    m_ProxyVec3dArray = vsp::ReadFileXSec( xsec_id, file_name );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetAirfoilUpperPnts( const string& xsec_id )
{
    m_ProxyVec3dArray = vsp::GetAirfoilUpperPnts( xsec_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetAirfoilLowerPnts( const string& xsec_id )
{
    m_ProxyVec3dArray = vsp::GetAirfoilLowerPnts( xsec_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::ReadBORFileXSec( const string& bor_id, const string& file_name )
{
    m_ProxyVec3dArray = vsp::ReadBORFileXSec( bor_id, file_name );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetBORAirfoilUpperPnts( const string& bor_id )
{
    m_ProxyVec3dArray = vsp::GetBORAirfoilUpperPnts( bor_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetBORAirfoilLowerPnts( const string& bor_id )
{
    m_ProxyVec3dArray = vsp::GetBORAirfoilLowerPnts( bor_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetGeomSetAtIndex( int index )
{
    m_ProxyStringArray = vsp::GetGeomSetAtIndex( index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeomSet( const string & name )
{
    m_ProxyStringArray = vsp::GetGeomSet( name );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllBackground3Ds()
{
    m_ProxyStringArray = vsp::GetAllBackground3Ds();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllBackground3DRelativePaths()
{
    m_ProxyStringArray = vsp::GetAllBackground3DRelativePaths();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllBackground3DAbsolutePaths()
{
    m_ProxyStringArray = vsp::GetAllBackground3DAbsolutePaths();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetMaterialNames()
{
    m_ProxyStringArray = vsp::GetMaterialNames();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::ListAnalysis()
{
    m_ProxyStringArray = vsp::ListAnalysis();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAnalysisInputNames( const string & analysis )
{
    m_ProxyStringArray = vsp::GetAnalysisInputNames( analysis );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetIntAnalysisInput( const string & analysis, const string & name, int index )
{
    m_ProxyIntArray = vsp::GetIntAnalysisInput( analysis, name, index );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetDoubleAnalysisInput( const string & analysis, const string & name, int index )
{
    m_ProxyDoubleArray = vsp::GetDoubleAnalysisInput( analysis, name, index );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetStringAnalysisInput( const string & analysis, const string & name, int index )
{
    m_ProxyStringArray = vsp::GetStringAnalysisInput( analysis, name, index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetVec3dAnalysisInput( const string & analysis, const string & name, int index )
{
    m_ProxyVec3dArray = vsp::GetVec3dAnalysisInput( analysis, name, index );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetAllGeometryAnalysesIDVec()
{
    m_ProxyStringArray = vsp::GetAllGeometryAnalysesIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAllAttributes()
{
    m_ProxyStringArray = vsp::FindAllAttributes();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAttributesByName( const string & search_str )
{
    m_ProxyStringArray = vsp::FindAttributesByName( search_str );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAttributedObjects()
{
    m_ProxyStringArray = vsp::FindAttributedObjects();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAttributesInCollection( const string &collID )
{
    m_ProxyStringArray = vsp::FindAttributesInCollection( collID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAttributesInCollectionByName( const string & obj_id, const string & search_str )
{
    m_ProxyStringArray = vsp::FindAttributesInCollectionByName( obj_id, search_str );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindAttributeNamesInCollection( const string & collID )
{
    m_ProxyStringArray = vsp::FindAttributeNamesInCollection( collID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeBoolVal( const string & attrID )
{
    m_ProxyIntArray = vsp::GetAttributeBoolVal( attrID );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeIntVal( const string & attrID )
{
    m_ProxyIntArray = vsp::GetAttributeIntVal( attrID );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeDoubleVal( const string & attrID )
{
    m_ProxyDoubleArray = vsp::GetAttributeDoubleVal( attrID );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeStringVal( const string & attrID )
{
    m_ProxyStringArray = vsp::GetAttributeStringVal( attrID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeParmID( const string & attrID )
{
    m_ProxyStringArray = vsp::GetAttributeParmID( attrID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeParmVal( const string & attrID )
{
    m_ProxyDoubleArray = vsp::GetAttributeParmVal( attrID );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeParmName( const string & attrID )
{
    m_ProxyStringArray = vsp::GetAttributeParmName( attrID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeVec3dVal( const string & attrID )
{
    m_ProxyVec3dArray = vsp::GetAttributeVec3dVal( attrID );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeIntMatrixVal( const string & attrID )
{
    m_ProxyIntMatArray = vsp::GetAttributeIntMatrixVal( attrID );
    return GetProxyIntMatArray();
}

CScriptArray* ScriptMgrSingleton::GetAttributeDoubleMatrixVal( const string & attrID )
{
    m_ProxyDoubleMatArray = vsp::GetAttributeDoubleMatrixVal( attrID );
    return GetProxyDoubleMatArray();
}

void ScriptMgrSingleton::SetAttributeVec3d( const string & attrID, CScriptArray* vec3dVector )
{
    vector < vec3d > vec3d_vec;
    FillSTLVector( vec3dVector, vec3d_vec );
    vsp::SetAttributeVec3d( attrID, vec3d_vec );
}

//===== vec2d polygon functions, bridged from a script array =====//
bool ScriptMgrSingleton::PointInPolygon( const vec2d & R, CScriptArray* pnts )
{
    vector < vec2d > pnt_vec;
    FillSTLVector( pnts, pnt_vec );
    return ::PointInPolygon( R, pnt_vec );
}

double ScriptMgrSingleton::PolyArea2d( CScriptArray* pnts )
{
    vector < vec2d > pnt_vec;
    FillSTLVector( pnts, pnt_vec );
    return ::poly_area( pnt_vec );
}

vec2d ScriptMgrSingleton::PolyCentroid2d( CScriptArray* pnts )
{
    vector < vec2d > pnt_vec;
    FillSTLVector( pnts, pnt_vec );
    return ::poly_centroid( pnt_vec );
}

void ScriptMgrSingleton::SetAttributeIntMatrix( const string & attrID, CScriptArray* intMatrix )
{
    vector < vector < int > > int_mat;
    FillSTLMatrix( intMatrix, int_mat );
    vsp::SetAttributeIntMatrix( attrID, int_mat );
}

void ScriptMgrSingleton::SetAttributeDoubleMatrix( const string & attrID, CScriptArray* dblMatrix )
{
    vector < vector < double > > dbl_mat;
    FillSTLMatrix( dblMatrix, dbl_mat );
    vsp::SetAttributeDoubleMatrix( attrID, dbl_mat );
}

string ScriptMgrSingleton::AddAttributeVec3d( const string & attrID, const string & attributeName, CScriptArray* vec3dVector )
{
    vector < vec3d > vec3d_vec;
    FillSTLVector( vec3dVector, vec3d_vec );
    string id = vsp::AddAttributeVec3d( attrID, attributeName, vec3d_vec );
    return id;
}

string ScriptMgrSingleton::AddAttributeIntMatrix( const string & collID, const string & attributeName, CScriptArray* intMatrix )
{
    vector < vector < int > > int_mat;
    FillSTLMatrix( intMatrix, int_mat );
    string id = vsp::AddAttributeIntMatrix( collID, attributeName, int_mat );
    return id;
}

string ScriptMgrSingleton::AddAttributeDoubleMatrix( const string & collID, const string & attributeName, CScriptArray* dblMatrix )
{
    vector < vector < double > > dbl_mat;
    FillSTLMatrix( dblMatrix, dbl_mat );
    string id = vsp::AddAttributeDoubleMatrix( collID, attributeName, dbl_mat );
    return id;
}

CScriptArray* ScriptMgrSingleton::PasteAttribute( const string & collID )
{
    m_ProxyStringArray = vsp::PasteAttribute( collID );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllResultsNames()
{
    m_ProxyStringArray = vsp::GetAllResultsNames();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllDataNames( const string & results_id )
{
    m_ProxyStringArray = vsp::GetAllDataNames( results_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetIntResults( const string & id, const string & name, int index )
{
    m_ProxyIntArray = vsp::GetIntResults( id, name, index );
    return GetProxyIntArray();
}

CScriptArray* ScriptMgrSingleton::GetDoubleResults( const string & id, const string & name, int index )
{
    m_ProxyDoubleArray = vsp::GetDoubleResults( id, name, index );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetDoubleMatResults(const string &id, const string &name, int index )
{
    m_ProxyDoubleMatArray = vsp::GetDoubleMatResults( id, name, index);
    return GetProxyDoubleMatArray();
}

CScriptArray* ScriptMgrSingleton::GetStringResults( const string & id, const string & name, int index )
{
    m_ProxyStringArray = vsp::GetStringResults( id, name, index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetVec3dResults( const string & id, const string & name, int index )
{
    m_ProxyVec3dArray = vsp::GetVec3dResults( id, name, index );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::FindContainers()
{
    m_ProxyStringArray = vsp::FindContainers();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindContainersWithName( const string & name )
{
    m_ProxyStringArray = vsp::FindContainersWithName( name );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindContainerGroupNames( const string & parm_container_id )
{
    m_ProxyStringArray = vsp::FindContainerGroupNames( parm_container_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindContainerParmIDs( const string & parm_container_id )
{
    m_ProxyStringArray = vsp::FindContainerParmIDs( parm_container_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllUserParms()
{
    m_ProxyStringArray = vsp::GetAllUserParms();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetUpperCSTCoefs( const string & xsec_id )
{
    m_ProxyDoubleArray = vsp::GetUpperCSTCoefs( xsec_id );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetLowerCSTCoefs( const string & xsec_id )
{
    m_ProxyDoubleArray = vsp::GetLowerCSTCoefs( xsec_id );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetAllRoutingPtIds( const string &routing_id )
{
    m_ProxyStringArray = vsp::GetAllRoutingPtIds( routing_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllRoutingPtCoords( const string &routing_id, int symm_index )
{
    m_ProxyVec3dArray = vsp::GetAllRoutingPtCoords( routing_id, symm_index );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetRoutingCurve( const string &routing_id, int symm_index )
{
    m_ProxyVec3dArray = vsp::GetRoutingCurve( routing_id, symm_index );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetBORUpperCSTCoefs( const string & bor_id )
{
    m_ProxyDoubleArray = vsp::GetBORUpperCSTCoefs( bor_id );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetBORLowerCSTCoefs( const string & bor_id )
{
    m_ProxyDoubleArray = vsp::GetBORLowerCSTCoefs( bor_id );
    return GetProxyDoubleArray();
}

void ScriptMgrSingleton::DeleteGeomVec( CScriptArray* del_arr, int clone_delete )
{
    vector < string > del_vec;
    FillSTLVector( del_arr, del_vec );

    vsp::DeleteGeomVec( del_vec, clone_delete );
}

CScriptArray* ScriptMgrSingleton::CloneGeomVec( CScriptArray* geom_arr, const string & name_suffix )
{
    vector < string > geom_vec;
    FillSTLVector( geom_arr, geom_vec );

    m_ProxyStringArray = vsp::CloneGeomVec( geom_vec, name_suffix );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::FindGeomClones( CScriptArray* geom_arr )
{
    vector < string > geom_vec;
    FillSTLVector( geom_arr, geom_vec );

    m_ProxyStringArray = vsp::FindGeomClones( geom_vec );
    return GetProxyStringArray();
}

void ScriptMgrSingleton::SetXSecPnts( const string& xsec_id, CScriptArray* pnt_arr )
{
    vector< vec3d > pnt_vec;
    FillSTLVector( pnt_arr, pnt_vec );

    vsp::SetXSecPnts( xsec_id, pnt_vec );
}

void ScriptMgrSingleton::SetAirfoilUpperPnts( const string& xsec_id, CScriptArray* up_pnt_arr )
{
    vector< vec3d > up_pnt_vec;
    up_pnt_vec.resize( up_pnt_arr->GetSize() );
    for ( int i = 0 ; i < ( int )up_pnt_arr->GetSize() ; i++ )
    {
        up_pnt_vec[i] = * ( vec3d* )( up_pnt_arr->At( i ) );
    }

    vsp::SetAirfoilUpperPnts( xsec_id, up_pnt_vec );
}

void ScriptMgrSingleton::SetAirfoilLowerPnts( const string& xsec_id, CScriptArray* low_pnt_arr )
{
    vector< vec3d > low_pnt_vec;
    low_pnt_vec.resize( low_pnt_arr->GetSize() );
    for ( int i = 0 ; i < ( int )low_pnt_arr->GetSize() ; i++ )
    {
        low_pnt_vec[i] = * ( vec3d* )( low_pnt_arr->At( i ) );
    }

    vsp::SetAirfoilLowerPnts( xsec_id, low_pnt_vec );
}

void ScriptMgrSingleton::SetAirfoilPnts( const string& xsec_id, CScriptArray* up_pnt_arr, CScriptArray* low_pnt_arr )
{
    vector< vec3d > up_pnt_vec;
    FillSTLVector( up_pnt_arr, up_pnt_vec );

    vector< vec3d > low_pnt_vec;
    FillSTLVector( low_pnt_arr, low_pnt_vec );

    vsp::SetAirfoilPnts( xsec_id, up_pnt_vec, low_pnt_vec );
}

CScriptArray* ScriptMgrSingleton::GetBORXSecPnts( const string& bor_id )
{
    m_ProxyVec3dArray = vsp::GetBORXSecPnts( bor_id );
    return GetProxyVec3dArray();
}

void ScriptMgrSingleton::SetBORXSecPnts( const string& bor_id, CScriptArray* pnt_arr )
{
    vector< vec3d > pnt_vec;
    FillSTLVector( pnt_arr, pnt_vec );

    vsp::SetBORXSecPnts( bor_id, pnt_vec );
}

void ScriptMgrSingleton::SetBORAirfoilUpperPnts( const string& bor_id, CScriptArray* up_pnt_arr )
{
    vector< vec3d > up_pnt_vec;
    up_pnt_vec.resize( up_pnt_arr->GetSize() );
    for ( int i = 0 ; i < ( int )up_pnt_arr->GetSize() ; i++ )
    {
        up_pnt_vec[i] = * ( vec3d* )( up_pnt_arr->At( i ) );
    }

    vsp::SetBORAirfoilUpperPnts( bor_id, up_pnt_vec );
}

void ScriptMgrSingleton::SetBORAirfoilLowerPnts( const string& bor_id, CScriptArray* low_pnt_arr )
{
    vector< vec3d > low_pnt_vec;
    low_pnt_vec.resize( low_pnt_arr->GetSize() );
    for ( int i = 0 ; i < ( int )low_pnt_arr->GetSize() ; i++ )
    {
        low_pnt_vec[i] = * ( vec3d* )( low_pnt_arr->At( i ) );
    }

    vsp::SetBORAirfoilLowerPnts( bor_id, low_pnt_vec );
}

void ScriptMgrSingleton::SetBORAirfoilPnts( const string& bor_id, CScriptArray* up_pnt_arr, CScriptArray* low_pnt_arr )
{
    vector< vec3d > up_pnt_vec;
    FillSTLVector( up_pnt_arr, up_pnt_vec );

    vector< vec3d > low_pnt_vec;
    FillSTLVector( low_pnt_arr, low_pnt_vec );

    vsp::SetBORAirfoilPnts( bor_id, up_pnt_vec, low_pnt_vec );
}

CScriptArray* ScriptMgrSingleton::GetAirfoilCoordinates( const std::string & geom_id, const double &foilsurf_u )
{
    m_ProxyVec3dArray = vsp::GetAirfoilCoordinates( geom_id, foilsurf_u );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetHersheyBarLiftDist( const int &npts, const double &alpha, const double &Vinf, const double &span, bool full_span_flag )
{
    m_ProxyVec3dArray = vsp::GetHersheyBarLiftDist( npts, alpha, Vinf, span, full_span_flag );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetHersheyBarDragDist( const int &npts, const double &alpha, const double &Vinf, const double &span, bool full_span_flag )
{
    m_ProxyVec3dArray = vsp::GetHersheyBarDragDist( npts, alpha, Vinf, span, full_span_flag );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetVKTAirfoilPnts( const int &npts, const double &alpha, const double &epsilon, const double &kappa, const double &tau )
{
    m_ProxyVec3dArray = vsp::GetVKTAirfoilPnts( npts, alpha, epsilon, kappa, tau );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetVKTAirfoilCpDist( const double &alpha, const double &epsilon, const double &kappa, const double &tau, CScriptArray* xyzdata )
{
    vector< vec3d > xyz_vec;
    FillSTLVector( xyzdata, xyz_vec );

    m_ProxyDoubleArray = vsp::GetVKTAirfoilCpDist( alpha, epsilon, kappa, tau, xyz_vec );

    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetEllipsoidSurfPnts( const vec3d& center, const vec3d& abc_rad, int u_npts, int w_npts )
{
    m_ProxyVec3dArray = vsp::GetEllipsoidSurfPnts( center, abc_rad, u_npts, w_npts );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetFeatureLinePnts( const string & geom_id )
{
    m_ProxyVec3dArray = vsp::GetFeatureLinePnts( geom_id );

    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetEllipsoidCpDist( CScriptArray* surf_pnt_arr, const vec3d& abc_rad, const vec3d& V_inf )
{
    vector< vec3d > surf_pnt_vec;
    FillSTLVector( surf_pnt_arr, surf_pnt_vec );

    m_ProxyDoubleArray = vsp::GetEllipsoidCpDist( surf_pnt_vec, abc_rad, V_inf );

    return GetProxyDoubleArray();
}

void ScriptMgrSingleton::SetUpperCST( const string& xsec_id, int deg, CScriptArray* coefs_arr )
{
    vector < double > coefs_vec;
    FillSTLVector( coefs_arr, coefs_vec );

    vsp::SetUpperCST( xsec_id, deg, coefs_vec );
}

void ScriptMgrSingleton::SetLowerCST( const string& xsec_id, int deg, CScriptArray* coefs_arr )
{
    vector < double > coefs_vec;
    FillSTLVector( coefs_arr, coefs_vec );

    vsp::SetLowerCST( xsec_id, deg, coefs_vec );
}

void ScriptMgrSingleton::SetBORUpperCST( const string& bor_id, int deg, CScriptArray* coefs_arr )
{
    vector < double > coefs_vec;
    FillSTLVector( coefs_arr, coefs_vec );

    vsp::SetBORUpperCST( bor_id, deg, coefs_vec );
}

void ScriptMgrSingleton::SetBORLowerCST( const string& bor_id, int deg, CScriptArray* coefs_arr )
{
    vector < double > coefs_vec;
    FillSTLVector( coefs_arr, coefs_vec );

    vsp::SetBORLowerCST( bor_id, deg, coefs_vec );
}

//==== Edit Curve XSec Functions ====//
CScriptArray* ScriptMgrSingleton::GetEditXSecUVec( const std::string& xsec_id )
{
    m_ProxyDoubleArray = vsp::GetEditXSecUVec( xsec_id );

    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::GetEditXSecCtrlVec( const std::string & xsec_id, bool non_dimensional )
{
    m_ProxyVec3dArray = vsp::GetEditXSecCtrlVec( xsec_id, non_dimensional );

    return GetProxyVec3dArray();
}

void ScriptMgrSingleton::SetEditXSecPnts( const string & xsec_id, CScriptArray* u_vec, CScriptArray* control_pts, CScriptArray* r_vec )
{
    vector < vec3d > control_pnt_vec;
    FillSTLVector( control_pts, control_pnt_vec );

    vector < double > new_u_vec;
    FillSTLVector( u_vec, new_u_vec );

    vector < double > new_r_vec;
    FillSTLVector( r_vec, new_r_vec );

    vsp::SetEditXSecPnts( xsec_id, new_u_vec, control_pnt_vec, new_r_vec );
}

CScriptArray* ScriptMgrSingleton::GetEditXSecFixedUVec( const std::string& xsec_id )
{
    vector < bool > temp_vec = vsp::GetEditXSecFixedUVec( xsec_id );

    m_ProxyIntArray.clear();
    m_ProxyIntArray.resize( temp_vec.size() );
    for ( size_t i = 0; i < temp_vec.size(); i++ )
    {
        // Cast bool to int
        m_ProxyIntArray[i] = (int)temp_vec[i];
    }

    return GetProxyIntArray();
}

void ScriptMgrSingleton::SetEditXSecFixedUVec( const string & xsec_id, CScriptArray* fixed_u_vec )
{
    vector < bool > new_fixed_u_vec;
    FillSTLVector( fixed_u_vec, new_fixed_u_vec );

    vsp::SetEditXSecFixedUVec( xsec_id, new_fixed_u_vec );
}

//==== Variable Preset Functions ====//

CScriptArray* ScriptMgrSingleton::GetVarPresetGroups()
{
    m_ProxyStringArray = vsp::GetVarPresetGroups();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetVarPresetSettings( const string &group_id )
{
    m_ProxyStringArray = vsp::GetVarPresetSettings( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetVarPresetParmIDs( const string &group_id )
{
    m_ProxyStringArray = vsp::GetVarPresetParmIDs( group_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetVarPresetParmVals( const string &setting_id )
{
    m_ProxyDoubleArray = vsp::GetVarPresetParmVals( setting_id );
    return GetProxyDoubleArray();
}

void ScriptMgrSingleton::SetVarPresetParmVals( const string& setting_id, CScriptArray* coefs_arr )
{
    vector < double > coefs_vec;
    FillSTLVector( coefs_arr, coefs_vec );
    vsp::SetVarPresetParmVals( setting_id, coefs_vec );
}

//==== Mode Functions ====//
CScriptArray* ScriptMgrSingleton::GetAllModes()
{
    m_ProxyStringArray = vsp::GetAllModes();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::ModeGetAllGroups( const string &mode_id )
{
    m_ProxyStringArray = vsp::ModeGetAllGroups( mode_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::ModeGetAllSettings( const string &mode_id )
{
    m_ProxyStringArray = vsp::ModeGetAllSettings( mode_id );
    return GetProxyStringArray();
}

//==== PCurve Functions ====//
void ScriptMgrSingleton::SetPCurve( const string& geom_id, const int & pcurveid, CScriptArray* t_arr, CScriptArray* val_arr, const int & newtype )
{
    vector < double > t_vec;
    FillSTLVector( t_arr, t_vec );

    vector < double > val_vec;
    FillSTLVector( val_arr, val_vec );

    vsp::SetPCurve( geom_id, pcurveid, t_vec, val_vec, newtype );
}

CScriptArray* ScriptMgrSingleton::PCurveGetTVec( const std::string & geom_id, const int & pcurveid )
{
    m_ProxyDoubleArray = vsp::PCurveGetTVec( geom_id, pcurveid );
    return GetProxyDoubleArray();
}

CScriptArray* ScriptMgrSingleton::PCurveGetValVec( const std::string & geom_id, const int & pcurveid )
{
    m_ProxyDoubleArray = vsp::PCurveGetValVec( geom_id, pcurveid );
    return GetProxyDoubleArray();
}

//==== Parasite Drag Tool Functions ====//
void ScriptMgrSingleton::AddExcrescence( const std::string & excresName, int excresType, double excresVal)
{
    vsp::AddExcrescence(excresName,excresType,excresVal);
}

void ScriptMgrSingleton::DeleteExcrescence(int index)
{
    vsp::DeleteExcrescence(index);
}

CScriptArray* ScriptMgrSingleton::GetFeaLayerIDVec( const string & material_id )
{
    m_ProxyStringArray = vsp::GetFeaLayerIDVec( material_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetGeomTextureIDVec( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetGeomTextureIDVec( geom_id );
    return GetProxyStringArray();
}


CScriptArray* ScriptMgrSingleton::GetFeaTrimPartIDVec( const string & trim_id )
{
    m_ProxyStringArray = vsp::GetFeaTrimPartIDVec( trim_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaAssemblyConnectionIDVec( const string & assembly_id )
{
    m_ProxyStringArray = vsp::GetFeaAssemblyConnectionIDVec( assembly_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFitModelVarVec()
{
    m_ProxyStringArray = vsp::GetFitModelVarVec();
    return GetProxyStringArray();
}

void ScriptMgrSingleton::AddFitModelTargetPts( const string & geom_id, int surf_indx, CScriptArray* pt_arr )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vsp::AddFitModelTargetPts( geom_id, surf_indx, pt_vec );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedU( const string & geom_id, int surf_indx, CScriptArray* pt_arr, double u )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vsp::AddFitModelTargetPtsFixedU( geom_id, surf_indx, pt_vec, u );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedUs( const string & geom_id, int surf_indx, CScriptArray* pt_arr, CScriptArray* u_arr )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vector < double > u_vec;
    FillSTLVector( u_arr, u_vec );

    vsp::AddFitModelTargetPtsFixedUs( geom_id, surf_indx, pt_vec, u_vec );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedW( const string & geom_id, int surf_indx, CScriptArray* pt_arr, double w )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vsp::AddFitModelTargetPtsFixedW( geom_id, surf_indx, pt_vec, w );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedWs( const string & geom_id, int surf_indx, CScriptArray* pt_arr, CScriptArray* w_arr )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vector < double > w_vec;
    FillSTLVector( w_arr, w_vec );

    vsp::AddFitModelTargetPtsFixedWs( geom_id, surf_indx, pt_vec, w_vec );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedUW( const string & geom_id, int surf_indx, CScriptArray* pt_arr, double u, double w )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vsp::AddFitModelTargetPtsFixedUW( geom_id, surf_indx, pt_vec, u, w );
}

void ScriptMgrSingleton::AddFitModelTargetPtsFixedUWs( const string & geom_id, int surf_indx, CScriptArray* pt_arr, CScriptArray* u_arr, CScriptArray* w_arr )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    vector < double > u_vec;
    FillSTLVector( u_arr, u_vec );

    vector < double > w_vec;
    FillSTLVector( w_arr, w_vec );

    vsp::AddFitModelTargetPtsFixedUWs( geom_id, surf_indx, pt_vec, u_vec, w_vec );
}

string ScriptMgrSingleton::CreatePtCloudGeomFromPts( CScriptArray* pt_arr, const string & name )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    return vsp::CreatePtCloudGeomFromPts( pt_vec, name );
}

CScriptArray* ScriptMgrSingleton::KeepPtsInBBox( CScriptArray* pt_arr, const vec3d & min_pt, const vec3d & max_pt )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsInBBox( pt_vec, min_pt, max_pt );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::RemovePtsInBBox( CScriptArray* pt_arr, const vec3d & min_pt, const vec3d & max_pt )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::RemovePtsInBBox( pt_vec, min_pt, max_pt );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::KeepPtsInRange( CScriptArray* pt_arr, int dir_index, double low, double high )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsInRange( pt_vec, dir_index, low, high );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::RemovePtsInRange( CScriptArray* pt_arr, int dir_index, double low, double high )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::RemovePtsInRange( pt_vec, dir_index, low, high );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::KeepPtsAbove( CScriptArray* pt_arr, int dir_index, double val )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsAbove( pt_vec, dir_index, val );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::KeepPtsBelow( CScriptArray* pt_arr, int dir_index, double val )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsBelow( pt_vec, dir_index, val );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::KeepPtsNearPt( CScriptArray* pt_arr, const vec3d & center, double radius )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsNearPt( pt_vec, center, radius );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::RemovePtsNearPt( CScriptArray* pt_arr, const vec3d & center, double radius )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::RemovePtsNearPt( pt_vec, center, radius );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::KeepPtsNearGeom( CScriptArray* pt_arr, const string & geom_id, int surf_indx, double tol )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::KeepPtsNearGeom( pt_vec, geom_id, surf_indx, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::RemovePtsNearGeom( CScriptArray* pt_arr, const string & geom_id, int surf_indx, double tol )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::RemovePtsNearGeom( pt_vec, geom_id, surf_indx, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::UniquePts( CScriptArray* pt_arr, double tol )
{
    vector < vec3d > pt_vec;
    FillSTLVector( pt_arr, pt_vec );

    m_ProxyVec3dArray = vsp::UniquePts( pt_vec, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::UnionPts( CScriptArray* pt_arr_a, CScriptArray* pt_arr_b, double tol )
{
    vector < vec3d > pt_vec_a;
    FillSTLVector( pt_arr_a, pt_vec_a );

    vector < vec3d > pt_vec_b;
    FillSTLVector( pt_arr_b, pt_vec_b );

    m_ProxyVec3dArray = vsp::UnionPts( pt_vec_a, pt_vec_b, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::IntersectPts( CScriptArray* pt_arr_a, CScriptArray* pt_arr_b, double tol )
{
    vector < vec3d > pt_vec_a;
    FillSTLVector( pt_arr_a, pt_vec_a );

    vector < vec3d > pt_vec_b;
    FillSTLVector( pt_arr_b, pt_vec_b );

    m_ProxyVec3dArray = vsp::IntersectPts( pt_vec_a, pt_vec_b, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::SubtractPts( CScriptArray* pt_arr_a, CScriptArray* pt_arr_b, double tol )
{
    vector < vec3d > pt_vec_a;
    FillSTLVector( pt_arr_a, pt_vec_a );

    vector < vec3d > pt_vec_b;
    FillSTLVector( pt_arr_b, pt_vec_b );

    m_ProxyVec3dArray = vsp::SubtractPts( pt_vec_a, pt_vec_b, tol );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetPtCloudPnts( const string & geom_id )
{
    m_ProxyVec3dArray = vsp::GetPtCloudPnts( geom_id );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaAssemblyIDVec()
{
    m_ProxyStringArray = vsp::GetFeaAssemblyIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaAssemblyStructureIDVec( const string & assembly_id )
{
    m_ProxyStringArray = vsp::GetFeaAssemblyStructureIDVec( assembly_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaMaterialIDVec()
{
    m_ProxyStringArray = vsp::GetFeaMaterialIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaPropertyIDVec()
{
    m_ProxyStringArray = vsp::GetFeaPropertyIDVec();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaStructIDVec()
{
    m_ProxyStringArray = vsp::GetFeaStructIDVec( );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaStructIDVecByGeom( const string & geom_id )
{
    m_ProxyStringArray = vsp::GetFeaStructIDVecByGeom( geom_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaSubSurfIDVec( const string & fea_struct_id )
{
    m_ProxyStringArray = vsp::GetFeaSubSurfIDVec( fea_struct_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllFeaPolySparPtIDVec( const string & pspar_id )
{
    m_ProxyStringArray = vsp::GetAllFeaPolySparPtIDVec( pspar_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaPartIDVec( const string & fea_struct_id )
{
    m_ProxyStringArray = vsp::GetFeaPartIDVec( fea_struct_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetFeaBCIDVec( const string & fea_struct_id )
{
    m_ProxyStringArray = vsp::GetFeaBCIDVec( fea_struct_id );
    return GetProxyStringArray();
}


void ScriptMgrSingleton::SetIntAnalysisInput( const string& analysis, const string & name, CScriptArray* indata, int index )
{
    vector < int > indata_vec;
    FillSTLVector( indata, indata_vec );

    vsp::SetIntAnalysisInput( analysis, name, indata_vec, index );
}

void ScriptMgrSingleton::SetDoubleAnalysisInput( const string& analysis, const string & name, CScriptArray* indata, int index )
{
    vector < double > indata_vec;
    FillSTLVector( indata, indata_vec );

    vsp::SetDoubleAnalysisInput( analysis, name, indata_vec, index );
}

void ScriptMgrSingleton::SetStringAnalysisInput( const string& analysis, const string & name, CScriptArray* indata, int index )
{
    vector < string > indata_vec;
    FillSTLVector( indata, indata_vec );

    vsp::SetStringAnalysisInput( analysis, name, indata_vec, index );
}

void ScriptMgrSingleton::SetVec3dAnalysisInput( const string& analysis, const string & name, CScriptArray* indata, int index )
{
    vector < vec3d > indata_vec;
    FillSTLVector( indata, indata_vec );

    vsp::SetVec3dAnalysisInput( analysis, name, indata_vec, index );
}

CScriptArray* ScriptMgrSingleton::CompVecPnt01(const string &geom_id, const int &surf_indx, CScriptArray* us, CScriptArray* ws)
{
    vector < double > in_us;
    FillSTLVector( us, in_us );

    vector < double > in_ws;
    FillSTLVector( ws, in_ws );

    m_ProxyVec3dArray = vsp::CompVecPnt01( geom_id, surf_indx, in_us, in_ws );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::CompVecDegenPnt01(const string &geom_id, const int &surf_indx, const int &degen_type, CScriptArray* us, CScriptArray* ws)
{
    vector < double > in_us;
    FillSTLVector( us, in_us );

    vector < double > in_ws;
    FillSTLVector( ws, in_ws );

    m_ProxyVec3dArray = vsp::CompVecDegenPnt01( geom_id, surf_indx, degen_type, in_us, in_ws );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::CompVecPntRST(const string &geom_id, const int &surf_indx, CScriptArray* rs, CScriptArray* ss, CScriptArray* ts)
{
    vector < double > in_rs;
    FillSTLVector( rs, in_rs );

    vector < double > in_ss;
    FillSTLVector( ss, in_ss );

    vector < double > in_ts;
    FillSTLVector( ts, in_ts );

    m_ProxyVec3dArray = vsp::CompVecPntRST( geom_id, surf_indx, in_rs, in_ss, in_ts );
    return GetProxyVec3dArray();
}

CScriptArray* ScriptMgrSingleton::CompVecNorm01(const string &geom_id, const int &surf_indx, CScriptArray* us, CScriptArray* ws)
{
    vector < double > in_us;
    FillSTLVector( us, in_us );

    vector < double > in_ws;
    FillSTLVector( ws, in_ws );

    m_ProxyVec3dArray = vsp::CompVecNorm01( geom_id, surf_indx, in_us, in_ws );
    return GetProxyVec3dArray();
}

void ScriptMgrSingleton::CompVecCurvature01(const string &geom_id, const int &surf_indx, CScriptArray* us, CScriptArray* ws, CScriptArray* k1s, CScriptArray* k2s, CScriptArray* kas, CScriptArray* kgs)
{
    vector < double > in_us;
    FillSTLVector( us, in_us );

    vector < double > in_ws;
    FillSTLVector( ws, in_ws );

    vector < double > out_k1s;
    vector < double > out_k2s;
    vector < double > out_kas;
    vector < double > out_kgs;

    vsp::CompVecCurvature01( geom_id, surf_indx, in_us, in_ws, out_k1s, out_k2s, out_kas, out_kgs );

    FillASArray( out_k1s, k1s );
    FillASArray( out_k2s, k2s );
    FillASArray( out_kas, kas );
    FillASArray( out_kgs, kgs );
}

void ScriptMgrSingleton::ProjVecPnt01(const string &geom_id, const int &surf_indx, CScriptArray* pts, CScriptArray* us, CScriptArray* ws, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > out_us;
    vector < double > out_ws;
    vector < double > out_ds;

    vsp::ProjVecPnt01( geom_id, surf_indx, in_pts, out_us, out_ws, out_ds );

    FillASArray( out_us, us );
    FillASArray( out_ws, ws );
    FillASArray( out_ds, ds );
}

void ScriptMgrSingleton::ProjVecPnt01Guess(const string &geom_id, const int &surf_indx, CScriptArray* pts, CScriptArray* u0s, CScriptArray* w0s, CScriptArray* us, CScriptArray* ws, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > in_u0s;
    FillSTLVector( u0s, in_u0s );

    vector < double > in_w0s;
    FillSTLVector( w0s, in_w0s );

    vector < double > out_us;
    vector < double > out_ws;
    vector < double > out_ds;

    vsp::ProjVecPnt01Guess( geom_id, surf_indx, in_pts, in_u0s, in_w0s, out_us, out_ws, out_ds );

    FillASArray( out_us, us );
    FillASArray( out_ws, ws );
    FillASArray( out_ds, ds );
}

void ScriptMgrSingleton::AxisProjVecPnt01(const string &geom_id, const int &surf_indx, const int &iaxis, CScriptArray* pts, CScriptArray* us, CScriptArray* ws, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > out_us;
    vector < double > out_ws;
    vector < double > out_ds;

    vsp::AxisProjVecPnt01( geom_id, surf_indx, iaxis, in_pts, out_us, out_ws, out_ds );

    FillASArray( out_us, us );
    FillASArray( out_ws, ws );
    FillASArray( out_ds, ds );
}

void ScriptMgrSingleton::AxisProjVecPnt01Guess(const string &geom_id, const int &surf_indx, const int &iaxis, CScriptArray* pts, CScriptArray* u0s, CScriptArray* w0s, CScriptArray* us, CScriptArray* ws, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > in_u0s;
    FillSTLVector( u0s, in_u0s );

    vector < double > in_w0s;
    FillSTLVector( w0s, in_w0s );

    vector < double > out_us;
    vector < double > out_ws;
    vector < double > out_ds;

    vsp::AxisProjVecPnt01Guess( geom_id, surf_indx, iaxis, in_pts, in_u0s, in_w0s, out_us, out_ws, out_ds );

    FillASArray( out_us, us );
    FillASArray( out_ws, ws );
    FillASArray( out_ds, ds );
}

CScriptArray* ScriptMgrSingleton::VecInsideSurf( const string &geom_id, const int &surf_indx, CScriptArray* pts )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < bool > temp_vec = vsp::VecInsideSurf( geom_id, surf_indx, in_pts );

    m_ProxyIntArray.clear();
    m_ProxyIntArray.resize( temp_vec.size() );
    for ( size_t i = 0; i < temp_vec.size(); i++ )
    {
        // Cast bool to int
        m_ProxyIntArray[i] = (int)temp_vec[i];
    }

    return GetProxyIntArray();
}

void ScriptMgrSingleton::FindRSTVec(const string &geom_id, const int &surf_indx, CScriptArray* pts, CScriptArray* rs, CScriptArray* ss, CScriptArray* ts, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > out_rs;
    vector < double > out_ss;
    vector < double > out_ts;
    vector < double > out_ds;

    vsp::FindRSTVec( geom_id, surf_indx, in_pts, out_rs, out_ss, out_ts, out_ds );

    FillASArray( out_rs, rs );
    FillASArray( out_ss, ss );
    FillASArray( out_ts, ts );
    FillASArray( out_ds, ds );
}

void ScriptMgrSingleton::FindRSTVecGuess(const string &geom_id, const int &surf_indx, CScriptArray* pts, CScriptArray* r0s, CScriptArray* s0s, CScriptArray* t0s, CScriptArray* rs, CScriptArray* ss, CScriptArray* ts, CScriptArray* ds )
{
    vector < vec3d > in_pts;
    FillSTLVector( pts, in_pts );

    vector < double > in_r0s;
    vector < double > in_s0s;
    vector < double > in_t0s;

    FillSTLVector( r0s, in_r0s );
    FillSTLVector( s0s, in_s0s );
    FillSTLVector( t0s, in_t0s );

    vector < double > out_rs;
    vector < double > out_ss;
    vector < double > out_ts;
    vector < double > out_ds;

    vsp::FindRSTVecGuess(geom_id, surf_indx, in_pts, in_r0s, in_s0s, in_t0s, out_rs, out_ss, out_ts, out_ds );

    FillASArray( out_rs, rs );
    FillASArray( out_ss, ss );
    FillASArray( out_ts, ts );
    FillASArray( out_ds, ds );
}

void ScriptMgrSingleton::ConvertRSTtoLMNVec(const string &geom_id, const int &surf_indx, CScriptArray* rs, CScriptArray* ss, CScriptArray* ts, CScriptArray* ls, CScriptArray* ms, CScriptArray* ns )
{
    vector < double > in_rs;
    vector < double > in_ss;
    vector < double > in_ts;

    FillSTLVector( rs, in_rs );
    FillSTLVector( ss, in_ss );
    FillSTLVector( ts, in_ts );

    vector < double > out_ls;
    vector < double > out_ms;
    vector < double > out_ns;

    vsp::ConvertRSTtoLMNVec( geom_id, surf_indx, in_rs, in_ss, in_ts, out_ls, out_ms, out_ns );

    FillASArray( out_ls, ls );
    FillASArray( out_ms, ms );
    FillASArray( out_ns, ns );
}

void ScriptMgrSingleton::ConvertLMNtoRSTVec(const string &geom_id, const int &surf_indx, CScriptArray* ls, CScriptArray* ms, CScriptArray* ns, CScriptArray* rs, CScriptArray* ss, CScriptArray* ts )
{
    vector < double > in_ls;
    vector < double > in_ms;
    vector < double > in_ns;

    FillSTLVector( ls, in_ls );
    FillSTLVector( ms, in_ms );
    FillSTLVector( ns, in_ns );

    vector < double > out_rs;
    vector < double > out_ss;
    vector < double > out_ts;

    vsp::ConvertLMNtoRSTVec( geom_id, surf_indx, in_ls, in_ms, in_ns, out_rs, out_ss, out_ts );

    FillASArray( out_rs, rs );
    FillASArray( out_ss, ss );
    FillASArray( out_ts, ts );
}

void ScriptMgrSingleton::GetUWTess01(const string &geom_id, const int &surf_indx, CScriptArray* us, CScriptArray* ws )
{
    vector < double > out_us;
    vector < double > out_ws;

    vsp::GetUWTess01( geom_id, surf_indx, out_us, out_ws );

    FillASArray( out_us, us );
    FillASArray( out_ws, ws );
}

CScriptArray* ScriptMgrSingleton::ControlSurfaceHingeLine( const std::string & id, const int & surf_indx )
{
    m_ProxyVec3dArray = vsp::ControlSurfaceHingeLine( id, surf_indx );
    return GetProxyVec3dArray();
}

//=== Register Measure Functions ===//
CScriptArray* ScriptMgrSingleton::GetAllProtractors()
{
    m_ProxyStringArray = vsp::GetAllProtractors();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllRSTProbes()
{
    m_ProxyStringArray = vsp::GetAllRSTProbes();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllRulers()
{
    m_ProxyStringArray = vsp::GetAllRulers();
    return GetProxyStringArray();}

CScriptArray* ScriptMgrSingleton::GetAllBogies( const string &gear_id )
{
    m_ProxyStringArray = vsp::GetAllBogies( gear_id );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAllProbes()
{
    m_ProxyStringArray = vsp::GetAllProbes();
    return GetProxyStringArray();
}

//==== Console Print String Data ====//

//==== Console Print Vec3d Data ====//

//==== Console Print Double Data ====//

//==== Console Print Int Data ====//

//=== Register Advanced Link Functions ===//
CScriptArray* ScriptMgrSingleton::GetAdvLinkNames()
{
    m_ProxyStringArray = vsp::GetAdvLinkNames();
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAdvLinkInputNames( int index )
{
    m_ProxyStringArray = vsp::GetAdvLinkInputNames( index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAdvLinkInputParms( int index )
{
    m_ProxyStringArray = vsp::GetAdvLinkInputParms( index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAdvLinkOutputNames( int index )
{
    m_ProxyStringArray = vsp::GetAdvLinkOutputNames( index );
    return GetProxyStringArray();
}

CScriptArray* ScriptMgrSingleton::GetAdvLinkOutputParms( int index )
{
    m_ProxyStringArray = vsp::GetAdvLinkOutputParms( index );
    return GetProxyStringArray();
}
