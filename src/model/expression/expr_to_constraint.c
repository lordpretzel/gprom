#include "common.h"
#include "model/expression/expression.h"
#include "model/expression/expr_to_constraint.h"
#include "mem_manager/mem_mgr.h"
#include "log/logger.h"
#include "model/node/nodetype.h"
#include "model/list/list.h"
#include "model/expression/expression.h"
#include "model/query_block/query_block.h"
#include "configuration/option.h"

#ifdef HAVE_LIBCPLEX
#include <ilcplex/cplex.h>
#endif

#define GPROM_INFBOUND 1e20

RenamingCtx *
newRenamingCtx ()
{
    RenamingCtx *ctx = MALLOC(sizeof(RenamingCtx));
    ctx->map = NEW_MAP(Constant, Constant);

    return ctx;
}

ConstraintTranslationCtx *
newConstraintTranslationCtx ()
{
    ConstraintTranslationCtx *ctx = MALLOC(sizeof(ConstraintTranslationCtx));
    ctx->current_expr = 0;
    ctx->variableMap = NEW_MAP(Constant, Constant);
    ctx->variables = NIL;
    ctx->constraints = NIL;
    ctx->caseConds = NIL;

    MAP_ADD_STRING_KEY(ctx->variableMap, "M", createConstInt(1000));
    MAP_ADD_STRING_KEY(ctx->variableMap, "-M", createConstInt(-1000)); //TODO: There is definitely a better way to be doing this.

    return ctx;
}

// Something to note:
// MILP variables will be reused for SQLParameters if they are present in the variableMap
SQLParameter *
introduceMILPVariable (Node *expr, ConstraintTranslationCtx *ctx, boolean isBoolean)
{
    SQLParameter *variable = NULL;

    if(isA(expr, SQLParameter))
    {
        SQLParameter *parameter = (SQLParameter *)expr;
        if(MAP_HAS_STRING_KEY(ctx->variableMap, parameter->name))
        {
            // TODO: Should probably respect isBoolean
            // TODO: Look into not allocating another SQLParameter perhaps?
            variable = (SQLParameter *)MAP_GET_STRING(ctx->variableMap, parameter->name);
            // variable = createSQLParameter(STRING_VALUE((Constant *)MAP_GET_STRING_ENTRY(ctx->variableMap, parameter->name)));
        }
        else
        {
             variable = createSQLParameter(CONCAT_STRINGS(isBoolean ? "b" : "v", gprom_itoa(++(ctx->current_expr))));
             if (isBoolean) variable->parType = DT_BOOL;
             MAP_ADD_STRING_KEY(ctx->variableMap, parameter->name, variable);
             ctx->variables = appendToTailOfList(ctx->variables, variable);
        }
    }
    else if(isA(expr, AttributeReference))
    {
        AttributeReference *reference = (AttributeReference *)expr;
        if(MAP_HAS_STRING_KEY(ctx->variableMap, reference->name))
        {
            // TODO: Should probably respect isBoolean
            // TODO: Look into not allocating another SQLParameter perhaps?
            variable = (SQLParameter *)MAP_GET_STRING(ctx->variableMap, reference->name);
            // variable = createSQLParameter(STRING_VALUE((Constant *)MAP_GET_STRING_ENTRY(ctx->variableMap, parameter->name)));
        }
        else
        {
            variable = createSQLParameter(CONCAT_STRINGS(isBoolean ? "b" : "v", gprom_itoa(++(ctx->current_expr))));
            if (isBoolean) variable->parType = DT_BOOL;
            MAP_ADD_STRING_KEY(ctx->variableMap, reference->name, variable);
            ctx->variables = appendToTailOfList(ctx->variables, variable);
        }
    }
    else
    {
        variable = createSQLParameter(CONCAT_STRINGS(isBoolean ? "b" : "v", gprom_itoa(++(ctx->current_expr))));
        if (isBoolean) variable->parType = DT_BOOL;
        // Let exprToConstraint append by itself...
        // ctx->variables = appendToTailOfList(ctx->variables, variable);
    }


    return variable;
}

// Extension of operator_optimizer.c:1403
static boolean
renameParameters (Node *node, RenamingCtx *ctx)
{
    HashMap *nameMap = ctx->map;
    if (node == NULL)
        return FALSE;

    if(isA(node,AttributeReference))
    {
        AttributeReference *a = (AttributeReference *) node;

        if (MAP_HAS_STRING_KEY(nameMap, a->name))
        {
            a->name = CONCAT_STRINGS(a->name, gprom_itoa(INT_VALUE(MAP_GET_STRING(nameMap, a->name))));
            return TRUE;
        }
    }
    else if(isA(node, SQLParameter))
    {
        SQLParameter *s = (SQLParameter *) node;

        if (MAP_HAS_STRING_KEY(nameMap, s->name)) {
            s->name = CONCAT_STRINGS(s->name, gprom_itoa(INT_VALUE(MAP_GET_STRING(nameMap, s->name))));
            return TRUE;
        }
    }
    else
        return visit(node, renameParameters, ctx);

    return FALSE;
}

List *
historyToCaseExprsFreshVars (List *history, RenamingCtx *ctx) {
    HashMap *current = ctx->map;
    List *caseExprs = NIL;
    // HashMap *current = NEW_MAP(Constant, Constant);
    // TODO: Deep copy history list? copyobject
    // Change variables and construct CASE exprs
    FOREACH(Update, u, history)
    {
        // TODO: handle multiple sets?
        Operator *set = (Operator *)getHeadOfListP(u->selectClause);
        AttributeReference *setAttr = (AttributeReference *)getHeadOfListP(set->args);
        Node *setExpr = (Node *)getTailOfListP(set->args);

        if(!MAP_HAS_STRING_KEY(current, setAttr->name)) {
            // first update rename will look like foo1 = foo0 + 3 where foo0 > 2
            MAP_ADD_STRING_KEY(current, setAttr->name, createConstInt(0));
        }

        char *rhsSetAttr = CONCAT_STRINGS(setAttr->name, gprom_itoa(INT_VALUE(MAP_GET_STRING(current, setAttr->name))));

        visit((Node*)setExpr, renameParameters, ctx);
        visit(u->cond, renameParameters, ctx);
        MAP_ADD_STRING_KEY(current, setAttr->name, createConstInt(INT_VALUE(MAP_GET_STRING(current, setAttr->name)) + 1));
        setAttr->name = CONCAT_STRINGS(setAttr->name, gprom_itoa(INT_VALUE(MAP_GET_STRING(current, setAttr->name)))); // visit break on AttributeReference

        // foo1 = case when foo0 > 3 then foo0 + 3 else 4
        caseExprs = appendToTailOfList(caseExprs, createOpExpr(":=", LIST_MAKE(createSQLParameter(setAttr->name), 
            createCaseExpr(NULL, singleton(createCaseWhen(u->cond, (Node *)getTailOfListP(set->args))), (Node *)createSQLParameter(rhsSetAttr))
        )));
    }
    return caseExprs;
}

// Taking SQL constraints from SelectItems and turning them into MILP constraints as described by the paper.
ConstraintTranslationCtx *
exprToConstraints (Node *expr, ConstraintTranslationCtx *ctx)
{
    if(isA(expr, Operator))
    {
        Operator *operator = (Operator *)expr;
        if(strcmp(operator->name, "<") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c1 = makeNode(Constraint);
            c1->sense = CONSTRAINT_GE;
            c1->rhs = 0;
            c1->originalExpr = (Node *)operator;
            c1->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)resultant)
            );

            Constraint *c2 = makeNode(Constraint);
            c2->sense = CONSTRAINT_G;
            c2->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M"));
            c2->originalExpr = (Node *)operator;
            c2->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)leftVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c1);
            ctx->constraints = appendToTailOfList(ctx->constraints, c2);
        }
        else if(strcmp(operator->name, ">") == 0)
        {
            Operator *as = createOpExpr("NOT", LIST_MAKE(createOpExpr("<=", operator->args)));
            exprToConstraints((Node*)as, ctx);
        }
        else if(strcmp(operator->name, "<=") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c1 = makeNode(Constraint);
            c1->sense = CONSTRAINT_G;
            c1->rhs = 0;
            c1->originalExpr = (Node *)operator;
            c1->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)resultant)
            );

            Constraint *c2 = makeNode(Constraint);
            c2->sense = CONSTRAINT_GE;
            c2->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M")); // TODO: rhs should likely be a pointer to a Constant so M can change
            c2->originalExpr = (Node *)operator;
            c2->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)leftVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c1);
            ctx->constraints = appendToTailOfList(ctx->constraints, c2);
        }
        else if(strcmp(operator->name, ">=") == 0)
        {
            Operator *as = createOpExpr("NOT", LIST_MAKE(createOpExpr("<", operator->args)));
            exprToConstraints((Node*)as, ctx);
        }
        else if(strcmp(operator->name, "AND") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, TRUE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, TRUE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c1 = makeNode(Constraint);
            c1->sense = CONSTRAINT_LE;
            c1->rhs = 1;
            c1->originalExpr = (Node *)operator;
            c1->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-2), (Node*)resultant)
            );

            Constraint *c2 = makeNode(Constraint);
            c2->sense = CONSTRAINT_GE;
            c2->rhs = 0;
            c2->originalExpr = (Node *)operator;
            c2->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-2), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c1);
            ctx->constraints = appendToTailOfList(ctx->constraints, c2);
        }
        else if(strcmp(operator->name, "OR") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, TRUE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, TRUE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c1 = makeNode(Constraint);
            c1->sense = CONSTRAINT_LE;
            c1->rhs = 0;
            c1->originalExpr = (Node *)operator;
            c1->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-2), (Node*)resultant)
            );

            Constraint *c2 = makeNode(Constraint);
            c2->sense = CONSTRAINT_GE;
            c2->rhs = 0;
            c2->originalExpr = (Node *)operator;
            c2->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c1);
            ctx->constraints = appendToTailOfList(ctx->constraints, c2);
        }
        else if(strcmp(operator->name, "NOT") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *notExpr = introduceMILPVariable(getHeadOfListP(operator->args), ctx, TRUE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            Constraint *c = makeNode(Constraint);
            c->sense = CONSTRAINT_E;
            c->rhs = 1;
            c->originalExpr = (Node *)operator;
            c->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)resultant),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)notExpr)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c);
        }
        else if(strcmp(operator->name, "+") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("v", gprom_itoa(ctx->current_expr)));
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c = makeNode(Constraint);
            c->sense = CONSTRAINT_E;
            c->rhs = 0;
            c->originalExpr = (Node *)operator;
            c->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c);
        }
        else if(strcmp(operator->name, "=") == 0)
        {
            SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("b", gprom_itoa(ctx->current_expr)));
            resultant->parType = DT_BOOL;
            ctx->variables = appendToTailOfList(ctx->variables, resultant);

            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c1 = makeNode(Constraint);
            c1->sense = CONSTRAINT_G;
            c1->rhs = 0;
            c1->originalExpr = (Node *)operator;
            c1->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)resultant)
            );

            Constraint *c2 = makeNode(Constraint);
            c2->sense = CONSTRAINT_GE;
            c2->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M"));
            c2->originalExpr = (Node *)operator;
            c2->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)resultant)
            );

            Constraint *c3 = makeNode(Constraint);
            c3->sense = CONSTRAINT_G;
            c3->rhs = 0;
            c3->originalExpr = (Node *)operator;
            c3->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)resultant)
            );

            Constraint *c4 = makeNode(Constraint);
            c4->sense = CONSTRAINT_GE;
            c4->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M"));
            c4->originalExpr = (Node *)operator;
            c4->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)rightVar),
                createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)resultant)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c1);
            ctx->constraints = appendToTailOfList(ctx->constraints, c2);
            ctx->constraints = appendToTailOfList(ctx->constraints, c3);
            ctx->constraints = appendToTailOfList(ctx->constraints, c4);
        }
        else if(strcmp(operator->name, ":=") == 0)
        {
            SQLParameter *leftVar = introduceMILPVariable(getHeadOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getHeadOfListP(operator->args), ctx);

            SQLParameter *rightVar = introduceMILPVariable(getTailOfListP(operator->args), ctx, FALSE);
            exprToConstraints(getTailOfListP(operator->args), ctx);

            // Make constraints for this expression
            Constraint *c = makeNode(Constraint);
            c->sense = CONSTRAINT_E;
            c->rhs = 0;
            c->originalExpr = (Node *)operator;
            c->terms = LIST_MAKE(
                createNodeKeyValue((Node*)createConstInt(1), (Node*)leftVar),
                createNodeKeyValue((Node*)createConstInt(-1), (Node*)rightVar)
            );

            ctx->constraints = appendToTailOfList(ctx->constraints, c);
        }
    }
    else if(isA(expr, CaseExpr)) 
    {
        CaseExpr *caseExpr = (CaseExpr *)expr;
        CaseWhen *caseWhen = (CaseWhen *)getHeadOfListP(caseExpr->whenClauses); // redundant cast?

        // TODO: Fold multiple CaseWhens into chain of if/else, only handles first when
        // TODO: only handles CASE caseWhenList optionalCaseElse END
        // e.g. foo = case when 3 > 2 then 2 + 4 else 

        SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("v", gprom_itoa(ctx->current_expr)));
        ctx->variables = appendToTailOfList(ctx->variables, resultant);

        SQLParameter *b = introduceMILPVariable(caseWhen->when, ctx, TRUE);
        exprToConstraints(caseWhen->when, ctx);
        ctx->caseConds = appendToTailOfList(ctx->caseConds, b);

        SQLParameter *v1 = introduceMILPVariable(caseWhen->then, ctx, FALSE);
        exprToConstraints(caseWhen->then, ctx);

        SQLParameter *v2 = introduceMILPVariable(caseExpr->elseRes, ctx, FALSE);
        exprToConstraints(caseExpr->elseRes, ctx);

        SQLParameter *vIf = introduceMILPVariable(NULL, ctx, FALSE);
        ctx->variables = appendToTailOfList(ctx->variables, vIf); // append manually because not calling exprToConstraints on anything here as its a slack variable

        SQLParameter *vElse = introduceMILPVariable(NULL, ctx, FALSE);
        ctx->variables = appendToTailOfList(ctx->variables, vElse);

        Constraint *c1 = makeNode(Constraint);
        c1->sense = CONSTRAINT_E;
        c1->rhs = 0;
        c1->originalExpr = (Node *)expr;
        c1->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vIf),
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vElse),
            createNodeKeyValue((Node*)createConstInt(-1), (Node*)resultant)
        );

        Constraint *c2 = makeNode(Constraint);
        c2->sense = CONSTRAINT_LE;
        c2->rhs = 0;
        c2->originalExpr = (Node *)expr;
        c2->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vIf),
            createNodeKeyValue((Node*)createConstInt(-1), (Node*)v1)
        );

        Constraint *c3 = makeNode(Constraint);
        c3->sense = CONSTRAINT_GE;
        c3->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M"));
        c3->originalExpr = (Node *)expr;
        c3->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vIf),
            createNodeKeyValue((Node*)createConstInt(-1), (Node*)v1),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)b)
        );

        Constraint *c4 = makeNode(Constraint);
        c4->sense = CONSTRAINT_LE;
        c4->rhs = 0;
        c4->originalExpr = (Node *)expr;
        c4->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vIf),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)b)
        );

        Constraint *c5 = makeNode(Constraint);
        c5->sense = CONSTRAINT_GE;
        c5->rhs = 0;
        c5->originalExpr = (Node *)expr;
        c5->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vIf),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)b)
        );

        Constraint *c6 = makeNode(Constraint);
        c6->sense = CONSTRAINT_LE;
        c6->rhs = 0;
        c6->originalExpr = (Node *)expr;
        c6->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vElse),
            createNodeKeyValue((Node*)createConstInt(-1), (Node*)v2)
        );

        Constraint *c7 = makeNode(Constraint);
        c7->sense = CONSTRAINT_LE;
        c7->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "M"));
        c7->originalExpr = (Node *)expr;
        c7->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vElse),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)b)
        );

        Constraint *c8 = makeNode(Constraint);
        c8->sense = CONSTRAINT_GE;
        c8->rhs = 0;
        c8->originalExpr = (Node *)expr;
        c8->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vElse),
            createNodeKeyValue((Node*)createConstInt(-1), (Node*)v2),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "M"), (Node*)b)
        );

        Constraint *c9 = makeNode(Constraint);
        c9->sense = CONSTRAINT_GE;
        c9->rhs = INT_VALUE(MAP_GET_STRING(ctx->variableMap, "-M"));
        c9->originalExpr = (Node *)expr;
        c9->terms = LIST_MAKE(
            createNodeKeyValue((Node*)createConstInt(1), (Node*)vElse),
            createNodeKeyValue((Node*)MAP_GET_STRING(ctx->variableMap, "-M"), (Node*)b)
        );

        // TODO Macro?
        ctx->constraints = appendToTailOfList(ctx->constraints, c1);
        ctx->constraints = appendToTailOfList(ctx->constraints, c2);
        ctx->constraints = appendToTailOfList(ctx->constraints, c3);
        ctx->constraints = appendToTailOfList(ctx->constraints, c4);
        ctx->constraints = appendToTailOfList(ctx->constraints, c5);
        ctx->constraints = appendToTailOfList(ctx->constraints, c6);
        ctx->constraints = appendToTailOfList(ctx->constraints, c7);
        ctx->constraints = appendToTailOfList(ctx->constraints, c8);
        ctx->constraints = appendToTailOfList(ctx->constraints, c9);
    }
    else if(isA(expr, Constant))
    {
        SQLParameter *resultant = createSQLParameter(CONCAT_STRINGS("v", gprom_itoa(ctx->current_expr)));
        ctx->variables = appendToTailOfList(ctx->variables, resultant);

        Constraint *constraint = makeNode(Constraint);
        constraint->sense = CONSTRAINT_E;
        constraint->rhs = INT_VALUE(expr);
        constraint->originalExpr = (Node *)expr;
        constraint->terms = LIST_MAKE(createNodeKeyValue((Node*)createConstInt(1), (Node*)resultant));

        ctx->constraints = appendToTailOfList(ctx->constraints, constraint);
    }
    return ctx;
}

LPProblem *
newLPProblem (ConstraintTranslationCtx* ctx)
{
    LPProblem *problem = MALLOC(sizeof(LPProblem));

    // columns
    problem->ccnt = getListLength(ctx->variables);
    problem->colname = MALLOC(sizeof(char*) * problem->ccnt);
    problem->obj = MALLOC(sizeof(double) * problem->ccnt);
    problem->lb = MALLOC(sizeof(double) * problem->ccnt);
    problem->ub = MALLOC(sizeof(double) * problem->ccnt);
    problem->types = MALLOC(sizeof(char) * problem->ccnt);
    for(int i = 0; i < problem->ccnt; i++) {
        SQLParameter *relatedParameter = (SQLParameter *)getNthOfListP(ctx->variables, i);

        problem->obj[i] = 0.0;
        problem->colname[i] = relatedParameter->name;
        switch(relatedParameter->parType)
        {
            case DT_BOOL:
                problem->lb[i] = 0.0;
                problem->ub[i] = 1.0;
                problem->types[i] = 'I';
                break;
            default:
                problem->lb[i] = -GPROM_INFBOUND;
                problem->ub[i] = GPROM_INFBOUND;
                problem->types[i] = 'C';
        }
    }
    problem->obj[0] = 1.0; // Experimental

    // rows
    problem->rcnt = getListLength(ctx->constraints);
    problem->rhs = MALLOC(sizeof(double) * problem->rcnt);
    problem->sense = MALLOC(sizeof(char) * problem->rcnt);
    for(int i = 0; i < problem->rcnt; i++) {
        Constraint *c = (Constraint*)getNthOfListP(ctx->constraints, i);
        problem->rhs[i] = c->rhs;
        switch(c->sense) {
            case CONSTRAINT_E:
                problem->sense[i] = 'E';
                break;
            case CONSTRAINT_LE:
                problem->sense[i] = 'L';
                break;
            case CONSTRAINT_GE:
                problem->sense[i] = 'G';
                break;
            case CONSTRAINT_L:
                problem->sense[i] = 'L';
                problem->rhs[i] -= 0.01; // x < 4 can be x <= 3.99
                break;
            case CONSTRAINT_G:
                problem->sense[i] = 'G';
                problem->rhs[i] += 0.01; // x > 4 can be x >= 4.01
                break;
            default:
                problem->sense[i] = 'U';
                ERROR_LOG("Unimplemented sense. This should never happen.");
        }
    }

    problem->rmatbeg = MALLOC(sizeof(int) * problem->rcnt);
    int terms = 0;
    FOREACH(Constraint, c, ctx->constraints) {
        terms += getListLength(c->terms);
    }
    problem->nzcnt = terms;
    problem->rmatind = MALLOC(sizeof(int) * terms);
    problem->rmatval = MALLOC(sizeof(double) * terms);
    int current = 0, i = 0;
    FOREACH(Constraint, c, ctx->constraints) {
        problem->rmatbeg[current] = i;
        FOREACH(KeyValue, kv, c->terms) {
            problem->rmatval[i] = INT_VALUE(kv->key);
            problem->rmatind[i] = genericListPos(ctx->variables, equal, kv->value);
            if(problem->rmatind[i] == -1)
            {
                ERROR_LOG("Something very wrong happened. Variable not found.");
                ERROR_LOG("Looking for variable %s in haystack", nodeToString(kv->value));
                FOREACH(SQLParameter, p, ctx->variables)
                {
                    ERROR_LOG("Variable %s", nodeToString(p));
                }
            }
            i++;
        }
        current++;
    }
    return problem;
}

char *cstringLPProblem(LPProblem *lp, boolean details) {
    StringInfo str = makeStringInfo();
    
    appendStringInfo(str, "Rcnt: %d, Ccnt: %d, Non-zero: %d, LB: %f, UB: %f\n\n", lp->rcnt, lp->ccnt, lp->nzcnt, lp->lb[0], lp->ub[0]);

    if(details)
    {
        for(int i = 0; i < lp->ccnt; i++)
        {
            appendStringInfo(str, "(Optimization coef. %f) %s is a %c between %f and %f\n", lp->obj[i], lp->colname[i], lp->types[i], lp->lb[i], lp->ub[i]);
        }
        appendStringInfo(str, "\n");
        for(int i = 0; i < lp->rcnt; i++)
        {
            int rowB = lp->rmatbeg[i];
            int rowE = i == lp->rcnt - 1 ? lp->nzcnt - 1 : lp->rmatbeg[i+1]-1;
            for(int j = rowB; j <= rowE; j++) {
                appendStringInfo(str, "%0.2f * %s ", lp->rmatval[j], lp->colname[lp->rmatind[j]]);
            }
            char *sense;
            switch(lp->sense[i])
            {
                case 'G':
                    sense = ">=";
                    break;
                case 'E':
                    sense = "=";
                    break;
                case 'L':
                    sense = "<=";
                    break;    
            }
            appendStringInfo(str, "%s %f\n", sense, lp->rhs[i]);
        }
    }

    return str->data;
}

int executeLPProblem(LPProblem *lp)
{
        int cplexStatus = 0;
        CPXENVptr cplexEnv = CPXopenCPLEX(&cplexStatus);
        if (cplexEnv == NULL) ERROR_LOG("Could not open CPLEX environment."); else INFO_LOG("CPLEX environment opened.");
        cplexStatus = CPXsetintparam(cplexEnv, CPXPARAM_ScreenOutput, CPX_ON);
        cplexStatus ^= CPXsetintparam(cplexEnv, CPXPARAM_Read_DataCheck, CPX_DATACHECK_ASSIST);
        if(cplexStatus) ERROR_LOG("Couldn't turn on screen output or data checking...");

        CPXLPptr cplexLp = CPXcreateprob(cplexEnv, &cplexStatus, "gpromlp");
        cplexStatus = CPXchgobjsen(cplexEnv, cplexLp, CPX_MAX);

        cplexStatus = CPXnewcols(cplexEnv, cplexLp, lp->ccnt, lp->obj, lp->lb, lp->ub, lp->types, lp->colname); // TODO: lb and ub
        if(cplexStatus) ERROR_LOG("CPLEX failure - CPXnewcols"); else INFO_LOG("CPXnewcols succeeded.");

        cplexStatus = CPXaddrows(cplexEnv, cplexLp, 0, lp->rcnt, lp->nzcnt, lp->rhs, lp->sense, lp->rmatbeg,
                        lp->rmatind, lp->rmatval, NULL, NULL);
        if(cplexStatus) ERROR_LOG("CPLEX failure - CPXaddrows"); else INFO_LOG("CPXaddrows succeded.");

        INFO_LOG("CPXgetnumnz %d", CPXgetnumnz(cplexEnv, cplexLp));

        cplexStatus = CPXmipopt(cplexEnv, cplexLp);
        if(cplexStatus) {
            INFO_LOG("Error evaluating LP.");
        }
        else
        {
            INFO_LOG("Evaluated LP.");
        }

        int result = CPXgetstat(cplexEnv, cplexLp);
        /* double x[lp->ccnt];
        switch(CPXgetstat(cplexEnv, cplexLp)) {
            case 101:
                INFO_LOG("Optimal solution found");
                CPXgetx(cplexEnv, cplexLp, x, 0, lp->ccnt-1);
                for(int i = 0; i < lp->ccnt; i++) INFO_LOG("Col. %s opt. val. is %f", lp->colname[i], x[i]);
                break;
            case 103:
                INFO_LOG("Integer infeasible.");
                break;
            case 110:
            case 114:
                INFO_LOG("No solution exists.");
                break;
            default:
                INFO_LOG("Something else... value %d", CPXgetstat(cplexEnv, cplexLp));
        } */

        cplexStatus = CPXfreeprob(cplexEnv, &cplexLp);
        cplexStatus = CPXcloseCPLEX(&cplexEnv);
        if(cplexStatus) ERROR_LOG("Problem closing CPLEX environment.");

        return result;
}

char *cstringConstraint(Constraint *constraint, boolean origin) {
    StringInfo str = makeStringInfo();
    
    FOREACH(KeyValue, term, constraint->terms)
    {
        appendStringInfo(str, "%d * %s ", INT_VALUE(term->key), ((SQLParameter *)term->value)->name);
    }
    
    char *sense;
    switch(constraint->sense)
    {
        case CONSTRAINT_E:
            sense = "=";
            break;
        case CONSTRAINT_G:
            sense = ">";
            break;
        case CONSTRAINT_GE:
            sense = ">=";
            break;
        case CONSTRAINT_L:
            sense = "<";
            break;
        case CONSTRAINT_LE:
            sense = "<=";
    }

    appendStringInfo(str, "%s %d", sense, constraint->rhs);
    if(origin) appendStringInfo(str, " (%s)", nodeToString(constraint->originalExpr));

    return str->data;
}