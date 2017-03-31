/*-----------------------------------------------------------------------------
 *
 * test_parser.c
 *
 *
 *      AUTHOR: lord_pretzel
 *
 *
 *
 *-----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem_manager/mem_mgr.h"
#include "log/logger.h"
#include "configuration/option.h"
#include "configuration/option_parser.h"
#include "model/list/list.h"
#include "model/node/nodetype.h"
//#include "parser/parse_internal.h"
#include "parser/parser.h"
//#include "metadata_lookup/metadata_lookup.h"
//#include "../src/parser/sql_parser.tab.h"
#include "rewriter.h"
#include "symbolic_eval/expr_to_constraint.h"
#include "symbolic_eval/whatif_algo.h"
#include "common.h"
#include "parser/parser.h"
#include "model/query_operator/query_operator.h"
#include "analysis_and_translate/analyzer.h"
#include "analysis_and_translate/translator.h"
#include "sql_serializer/sql_serializer.h"
#include "analysis_and_translate/translator_oracle.h"
#include "provenance_rewriter/prov_rewriter.h"

int main(int argc, char* argv[]) {
	List *result;

// initialize components
	READ_OPTIONS_AND_INIT("testexprcplex", "Run expr to cplex.");

	// read from terminal
	if (getStringOption("input.query") == NULL) {
		FATAL_LOG("give -query");
	}
	// parse input string
	else {
		result = (List *) parseFromString(getStringOption("input.query"));

		//DEBUG_LOG("PARSE RESULT FROM STRING IS:\n%s", nodeToString(result));
		//DEBUG_LOG("PARSE RESULT FROM STRING IS:\n%s", beatify(nodeToString(result)));

		//ERROR_LOG("Result of CPLEX IS: %d \n", checkCplex((List *) result));

		List *updates = dependAlgo(result);

		ERROR_LOG("total Number of updates: %d \n", result->length - 1);
		ERROR_LOG("Number of dependent statements: %d \n", updates->length - 2);
		//DEBUG_LOG("PARSE RESULT FROM STRING IS:\n%s", nodeToString(updates));
		//DEBUG_LOG("PARSE RESULT FROM STRING IS:\n%s", beatify(nodeToString(updates)));

		Node *originalUp;
		originalUp = popHeadOfListP(updates);

		ProvenanceStmt *provStat;
		char *sql;
		Node *qoModel;
		provStat = createProvenanceStmt((Node *) updates);
		provStat->provType = PROV_NONE;
		provStat->inputType = PROV_INPUT_REENACT;
		DEBUG_NODE_BEATIFY_LOG("prov:\n", provStat);
		provStat = (ProvenanceStmt *) analyzeParseModel((Node *) provStat);
		DEBUG_NODE_BEATIFY_LOG("analysis:\n", provStat);
		qoModel = translateParseOracle((Node *) provStat);
		DEBUG_NODE_BEATIFY_LOG("qo model:\n", provStat);
		qoModel = provRewriteQBModel(qoModel);
		DEBUG_NODE_BEATIFY_LOG("after prov rewrite:\n", provStat);
		//qoModel = translateParse((Node *) provStat);
		sql = serializeOperatorModel(qoModel);
		ERROR_LOG("SERIALIZED SQL Reenactment For Whatif Query:\n%s", sql);

		//replace new update with original updates and create reenactment again
		popHeadOfListP(updates);
		updates = appendToHeadOfList(updates, originalUp);
		provStat = createProvenanceStmt((Node *) updates);
		provStat->provType = PROV_NONE;
		provStat->inputType = PROV_INPUT_REENACT;
		DEBUG_NODE_BEATIFY_LOG("prov:\n", provStat);
		provStat = (ProvenanceStmt *) analyzeParseModel((Node *) provStat);
		DEBUG_NODE_BEATIFY_LOG("analysis:\n", provStat);
		qoModel = translateParseOracle((Node *) provStat);
		DEBUG_NODE_BEATIFY_LOG("qo model:\n", provStat);
		qoModel = provRewriteQBModel(qoModel);
		DEBUG_NODE_BEATIFY_LOG("after prov rewrite:\n", provStat);
		sql = serializeOperatorModel(qoModel);
		ERROR_LOG("SERIALIZED SQL Reenactment For Original update:\n%s", sql);

////////////////////////////
		/*
		 Node *originalUp;

		 ProvenanceStmt *provStat;
		 char *sql;
		 Node *qoModel;
		 originalUp = popHeadOfListP(result);

		 provStat = createProvenanceStmt((Node *) result);
		 provStat->provType = PROV_NONE;
		 provStat->inputType = PROV_INPUT_REENACT;
		 DEBUG_NODE_BEATIFY_LOG("prov:\n", provStat);
		 provStat = (ProvenanceStmt *) analyzeParseModel((Node *) provStat);
		 DEBUG_NODE_BEATIFY_LOG("analysis:\n", provStat);
		 qoModel = translateParseOracle((Node *) provStat);
		 DEBUG_NODE_BEATIFY_LOG("qo model:\n", provStat);
		 qoModel = provRewriteQBModel(qoModel);
		 DEBUG_NODE_BEATIFY_LOG("after prov rewrite:\n", provStat);
		 //qoModel = translateParse((Node *) provStat);
		 sql = serializeOperatorModel(qoModel);
		 ERROR_LOG("All SERIALIZED SQL Reenactment For Whatif Query:\n%s", sql);

		 //replace new update with original updates and create reenactment again
		 popHeadOfListP(result);
		 result = appendToHeadOfList(result, originalUp);
		 provStat = createProvenanceStmt((Node *) result);
		 provStat->provType = PROV_NONE;
		 provStat->inputType = PROV_INPUT_REENACT;
		 DEBUG_NODE_BEATIFY_LOG("prov:\n", provStat);
		 provStat = (ProvenanceStmt *) analyzeParseModel((Node *) provStat);
		 DEBUG_NODE_BEATIFY_LOG("analysis:\n", provStat);
		 qoModel = translateParseOracle((Node *) provStat);
		 DEBUG_NODE_BEATIFY_LOG("qo model:\n", provStat);
		 qoModel = provRewriteQBModel(qoModel);
		 DEBUG_NODE_BEATIFY_LOG("after prov rewrite:\n", provStat);
		 sql = serializeOperatorModel(qoModel);
		 ERROR_LOG("All SERIALIZED SQL Reenactment For Original update:\n%s",
		 sql);
		 */

	}

	shutdownApplication();

	return EXIT_SUCCESS;
}
