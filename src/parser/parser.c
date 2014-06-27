/*-----------------------------------------------------------------------------
 *
 * parser.c
 *			  
 *		
 *		AUTHOR: lord_pretzel
 *
 *		
 *
 *-----------------------------------------------------------------------------
 */

#include "parser/parser.h"
#include "parser/parser_oracle.h"
#include "parser/parser_postgres.h"
#include "model/node/nodetype.h"
#include "mem_manager/mem_mgr.h"
#include "log/logger.h"
#include "instrumentation/timing_instrumentation.h"

// plugin
static ParserPlugin *plugin = NULL;

// function defs
//static Node *parseInternal (void);

static ParserPlugin *assembleOraclePlugin(void);
static ParserPlugin *assemblePostgresPlugin(void);
static ParserPlugin *assembleHivePlugin(void);

// wrapper interface
Node *
parseStream (FILE *stream)
{
    ASSERT(plugin);
    return plugin->parseStream(stream);
}

Node *
parseFromString (char *input)
{
    ASSERT(plugin);

    INFO_LOG("parse SQL:\n%s", input);
    return plugin->parseFromString(input);
}

// plugin management
void
chooseParserPlugin(ParserPluginType type)
{
    switch(type)
    {
        case PARSER_PLUGIN_ORACLE:
            plugin = assembleOraclePlugin();
            break;
        case PARSER_PLUGIN_POSTGRES:
            plugin = assemblePostgresPlugin();
            break;
        case PARSER_PLUGIN_HIVE:
            plugin = assembleHivePlugin();
            break;
    }
}

static ParserPlugin *
assembleOraclePlugin(void)
{
    ParserPlugin *p = NEW(ParserPlugin);

    p->parseStream = parseStreamOracle;
    p->parseFromString = parseFromStringOracle;

    return p;
}

static ParserPlugin *
assemblePostgresPlugin(void)
{
    ParserPlugin *p = NEW(ParserPlugin);

    p->parseStream = parseStreamPostgres;
    p->parseFromString = parseFromStringPostgres;

    return p;
}

static ParserPlugin *
assembleHivePlugin(void)
{
    ParserPlugin *p = NEW(ParserPlugin);

    p->parseStream = parseStreamOracle;
    p->parseFromString = parseFromStringOracle;
    FATAL_LOG("not support yet");

    return p;
}

void
chooseParserPluginFromString(char *type)
{
    INFO_LOG("PLUGIN parser: <%s>", type);

    if (streq(type,"oracle"))
        chooseParserPlugin(PARSER_PLUGIN_ORACLE);
    else if (streq(type,"postgres"))
        chooseParserPlugin(PARSER_PLUGIN_POSTGRES);
    else if (streq(type,"hive"))
        chooseParserPlugin(PARSER_PLUGIN_HIVE);
    else
        FATAL_LOG("unkown parser plugin type: <%s>", type);
}
