#include "common.h"
#include "model/query_operator/query_operator.h"

extern boolean isSemiringCombinerActivatedOp(QueryOperator *op);
extern boolean isSemiringCombinerActivatedPs(ProvenanceStmt *stmt);
extern char *getSemiringCombinerFuncName(QueryOperator *op);
extern Node *getSemiringCombinerExpr(QueryOperator *op);
extern DataType getSemiringCombinerDatatype(ProvenanceStmt *stmt, List *dts);
extern QueryOperator *addSemiringCombiner (QueryOperator *result, char * funcN, Node * expr);
