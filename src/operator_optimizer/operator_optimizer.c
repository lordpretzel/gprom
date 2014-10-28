/*-----------------------------------------------------------------------------
 *
 * operator_optimizer.c
 *			  
 *		
 *		AUTHOR: lord_pretzel
 *
 *		
 *
 *-----------------------------------------------------------------------------
 */

#include "common.h"
#include "configuration/option.h"
#include "instrumentation/timing_instrumentation.h"
#include "log/logger.h"
#include "operator_optimizer/operator_optimizer.h"
#include "operator_optimizer/operator_merge.h"
#include "operator_optimizer/expr_attr_factor.h"
#include "model/query_block/query_block.h"
#include "model/list/list.h"
#include "model/query_operator/schema_utility.h"
#include "model/query_operator/query_operator_model_checker.h"
#include "model/query_operator/operator_property.h"
#include "provenance_rewriter/prov_utility.h"

static QueryOperator *optimizeOneGraph (QueryOperator *root);
static QueryOperator *pullup(QueryOperator *op, List *duplicateattrs, List *normalAttrNames);

Node  *
optimizeOperatorModel (Node *root)
{
    if(isA(root, List))
    {
        FOREACH_LC(lc, (List *) root)
        {
            QueryOperator *o = (QueryOperator *) LC_P_VAL(lc);

            o = optimizeOneGraph(o);
            LC_P_VAL(lc) = o;
        }

        return root;
    }
    else
        return (Node *) optimizeOneGraph((QueryOperator *) root);
}

static QueryOperator *
optimizeOneGraph (QueryOperator *root)
{
    QueryOperator *rewrittenTree = root;

    if(getBoolOption(OPTIMIZATION_FACTOR_ATTR_IN_PROJ_EXPR))
    {
        START_TIMER("OptimizeModel - factor attributes in conditions");
        rewrittenTree = factorAttrsInExpressions((QueryOperator *) rewrittenTree);
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        DEBUG_LOG("factor out attribute references in conditions\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        STOP_TIMER("OptimizeModel - factor attributes in conditions");
    }

    if(getBoolOption(OPTIMIZATION_MERGE_OPERATORS))
    {
        START_TIMER("OptimizeModel - merge adjacent operator");
        rewrittenTree = mergeAdjacentOperators((QueryOperator *) rewrittenTree);
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        DEBUG_LOG("merged adjacent\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        STOP_TIMER("OptimizeModel - merge adjacent operator");
    }

    if(getBoolOption(OPTIMIZATION_SELECTION_PUSHING))
    {
        START_TIMER("OptimizeModel - pushdown selections");
        rewrittenTree = pushDownSelectionOperatorOnProv((QueryOperator *) rewrittenTree);
        DEBUG_LOG("selections pushed down\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        STOP_TIMER("OptimizeModel - pushdown selections");
    }

    if(getBoolOption(OPTIMIZATION_FACTOR_ATTR_IN_PROJ_EXPR))
    {
        START_TIMER("OptimizeModel - factor attributes in conditions");
        rewrittenTree = factorAttrsInExpressions((QueryOperator *) rewrittenTree);
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        DEBUG_LOG("factor out attribute references in conditions again\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        STOP_TIMER("OptimizeModel - factor attributes in conditions");
    }

    if(getBoolOption(OPTIMIZATION_MERGE_OPERATORS))
    {
        START_TIMER("OptimizeModel - merge adjacent operator");
        rewrittenTree = mergeAdjacentOperators((QueryOperator *) rewrittenTree);
        DEBUG_LOG("merged adjacent\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        STOP_TIMER("OptimizeModel - merge adjacent operator");
    }

    if(getBoolOption(OPTIMIZATION_REMOVE_REDUNDANT_PROJECTIONS))
    {
      START_TIMER("OptimizeModel - remove redundant projections");
      rewrittenTree = removeRedundantProjections((QueryOperator *) rewrittenTree);
      DEBUG_LOG("remove redundant projections\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
      ASSERT(checkModel((QueryOperator *) rewrittenTree));
      STOP_TIMER("OptimizeModel - set materialization hints");
    }

    if(getBoolOption(OPTIMIZATION_PULLING_UP_PROVENANCE_PROJ))
    {
      START_TIMER("OptimizeModel - pulling up provenance");
      rewrittenTree = pullingUpProvenanceProjections((QueryOperator *) rewrittenTree);
      DEBUG_LOG("pulling up provenance projections\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
      ASSERT(checkModel((QueryOperator *) rewrittenTree));
      STOP_TIMER("OptimizeModel - pulling up provenance");
    }

    if(getBoolOption(OPTIMIZATION_MERGE_OPERATORS))
    {
        START_TIMER("OptimizeModel - merge adjacent operator");
        rewrittenTree = mergeAdjacentOperators((QueryOperator *) rewrittenTree);
        DEBUG_LOG("merged adjacent\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        TIME_ASSERT(checkModel((QueryOperator *) rewrittenTree));
        STOP_TIMER("OptimizeModel - merge adjacent operator");
    }

    if(getBoolOption(OPTIMIZATION_MATERIALIZE_MERGE_UNSAFE_PROJ))
    {
        START_TIMER("OptimizeModel - set materialization hints");
        rewrittenTree = materializeProjectionSequences((QueryOperator *) rewrittenTree);
        DEBUG_LOG("add materialization hints for projection sequences\n\n%s", operatorToOverviewString((Node *) rewrittenTree));
        ASSERT(checkModel((QueryOperator *) rewrittenTree));
        STOP_TIMER("OptimizeModel - set materialization hints");
    }

    return rewrittenTree;
}

QueryOperator *
materializeProjectionSequences (QueryOperator *root)
{
    QueryOperator *lChild = OP_LCHILD(root);

    // if two adjacent projections then materialize the lower one
    if (isA(root, ProjectionOperator) && isA(lChild, ProjectionOperator))
        SET_BOOL_STRING_PROP(lChild, PROP_MATERIALIZE);

    FOREACH(QueryOperator,o,root->inputs)
        materializeProjectionSequences(o);

    return root;
}


QueryOperator *
mergeAdjacentOperators (QueryOperator *root)
{
    if (isA(root, SelectionOperator) && isA(OP_LCHILD(root), SelectionOperator))
        root = (QueryOperator *) mergeSelection((SelectionOperator *) root);
    if (isA(root, ProjectionOperator) && isA(OP_LCHILD(root), ProjectionOperator))
        root = (QueryOperator *) mergeProjection((ProjectionOperator *) root);

    FOREACH(QueryOperator,o,root->inputs)
         mergeAdjacentOperators(o);

    return root;
}


QueryOperator *
pushDownSelectionOperatorOnProv(QueryOperator *root)
{
    QueryOperator *newRoot = root;

	if (isA(root, SelectionOperator) && isA(OP_LCHILD(root), ProjectionOperator))
		newRoot = pushDownSelectionWithProjection((SelectionOperator *) root);

	FOREACH(QueryOperator, o, newRoot->inputs)
		pushDownSelectionOperatorOnProv(o);

	return newRoot;
}

QueryOperator *
factorAttrsInExpressions(QueryOperator *root)
{
    QueryOperator *newRoot = root;

    if (isA(root, ProjectionOperator))
        newRoot = projectionFactorAttrReferences((ProjectionOperator *) root);

    FOREACH(QueryOperator, o, newRoot->inputs)
        factorAttrsInExpressions(o);

    return root;
}


QueryOperator *
removeRedundantProjections(QueryOperator *root)
{
  QueryOperator *lChild = OP_LCHILD(root);

  if (isA(root, ProjectionOperator))
  {
      boolean compare = TRUE;
      List *l1 = ((ProjectionOperator *)root)->projExprs;
      List *l2 = lChild->schema->attrDefs;
      int i = 0;

      if (LIST_LENGTH(l1) != LIST_LENGTH(l2))
          compare = FALSE;
      else
      {
          FORBOTH_LC(lc1,lc2,l1,l2)
          {
              AttributeReference *x = (AttributeReference *)LC_P_VAL(lc1);
              AttributeDef *y = (AttributeDef *)LC_P_VAL(lc2);

              if (!streq(x->name,y->attrName) || i++ != x->attrPosition)
              {
                  compare = FALSE;
                  break;
              }
          }
      }

      if (compare)
      {
          // Remove Parent and make lChild as the new parent
          removeParentFromOps(root->inputs, (QueryOperator *) root);
          root = lChild;
      }
  }

  FOREACH(QueryOperator, o, root->inputs)
    removeRedundantProjections(o);

  return root;
}

QueryOperator *
pullingUpProvenanceProjections(QueryOperator *root)
{
    FOREACH(QueryOperator, o, root->inputs)
    {
        if(isA(o, ProjectionOperator))
        {
            if(HAS_STRING_PROP(o, PROP_PROJ_PROV_ATTR_DUP))
            {
                if(GET_BOOL_STRING_PROP(o, PROP_PROJ_PROV_ATTR_DUP) == TRUE)
                {
                    ProjectionOperator *op = (ProjectionOperator *)o;

                    //Get the attrReference of the provenance attribute
                    List *l1 = getProvenanceAttrReferences(op, o);

                    //Get the attrDef name of the provenance attribute
                    List *l2 = getOpProvenanceAttrNames(o);

                    //Get the attrReference of non provenance attribute
                    List *l3 = getNormalAttrReferences(op, o);

                    //Get the attrDef name in the schema of non provenance
                    //attribute
                    List *l4 = getNormalAttrNames(o);

                    List *l_prov_attr = NIL;

                    FOREACH(AttributeReference, a, l1)
                        l_prov_attr = appendToTailOfList(l_prov_attr, a->name);

                    List *l_normal_attr = NIL;

                    FOREACH(AttributeReference, a, l3)
                        l_normal_attr =  appendToTailOfList(l_normal_attr, a->name);

                    List *normalAttrNames = NIL;
                    List *duplicateattrs = NIL;

                    FORBOTH_LC(lc1, lc2, l_prov_attr, l2)
		    {
                        FORBOTH_LC(lc3 ,lc4, l_normal_attr, l4)
                        {
                            if(streq(lc1->data.ptr_value, lc3->data.ptr_value))
                            {
                                duplicateattrs = appendToTailOfList(duplicateattrs,lc2->data.ptr_value);
                                normalAttrNames = appendToTailOfList(normalAttrNames, lc4->data.ptr_value);
                                break;
                            }
                        }
                    }

                    //Delete the duplicateattrs from the provenance projection
                    //operator
                    FOREACH_LC(d,duplicateattrs)
                    {
                        //Delete the duplicate attr_ref from the projExprs
                        int pos = getAttrPos(o, LC_P_VAL(d));
                        deleteAttrRefFromProjExprs((ProjectionOperator *)op, pos);

                        //Delete the duplicate attr_def from the schema
                        deleteAttrFromSchemaByName((QueryOperator *)op, LC_P_VAL(d));
                    }

                    //root =
                    pullup(o, duplicateattrs, normalAttrNames);
                }
            }
        }

        pullingUpProvenanceProjections(o);
    }

    return root;
}


/*
 * duplicateattrs store attrDef name of provenance attribute, normalAttrnames
 * store attrRef name of provenance attribute
 */
QueryOperator *
pullup(QueryOperator *op, List *duplicateattrs, List *normalAttrNames)
{
    boolean fd = FALSE;
    boolean isLost= FALSE;

    // used to store the name of lost attributes, LostList-duplicateattrs,
    // LostNormalList-normalAttrnames
    List* LostList = NIL;
    List* LostNormalList = NIL;

    List* duplicateattrsCopy = copyList(duplicateattrs);
    List* normalAttrNamesCopy = copyList(normalAttrNames);

    FOREACH(QueryOperator, o, op->parents)
    {
        FORBOTH_LC(d, nms, duplicateattrs, normalAttrNames)
        {
            // find the lost attribute, if we do not find it, we need to add
            // projection op; or continue upward check.
            fd = FALSE;
            if(isA(o, ProjectionOperator))
            {
                FOREACH(AttributeReference,a ,((ProjectionOperator *)o)->projExprs)
                {
                    if (streq(a->name, nms->data.ptr_value))
                    {
                        fd = TRUE;
                        break;
                    }
                }
            }
            else
            {
                FOREACH_LC(a ,o->schema->attrDefs)
                {
                    if (streq(((AttributeDef *) a->data.ptr_value)->attrName, nms->data.ptr_value))
                    {
                        fd = TRUE;
                        break;
                    }
                }
            }

            if(!fd)
            {
                isLost = TRUE;

                //add d to the list which stores the name of lost attributes
                LostList = appendToTailOfList(LostList, d->data.ptr_value);
                LostNormalList = appendToTailOfList(LostNormalList, nms->data.ptr_value);

                //get rid of the attribute from the duplicate list and
                //normalAttrnames
                duplicateattrsCopy = REMOVE_FROM_LIST_PTR(duplicateattrsCopy, d->data.ptr_value);
		normalAttrNamesCopy = REMOVE_FROM_LIST_PTR(normalAttrNamesCopy, nms->data.ptr_value);
            }
            else
            {
                //If not projection op, just get rid of the attrDef from
                //schema. If projection op get rid of the attrDef from schema
                //and attrRef from projExprs
                if(isA(o, ProjectionOperator))
                {
                    //Get rid of the attrDef from schema and attrRef from projExprs
		    int pos = getAttrPos((QueryOperator *)o, LC_P_VAL(d));
                    deleteAttrRefFromProjExprs((ProjectionOperator *)o, pos);

                    List *normalAttrNamesCopyTempList = NIL;
                    boolean nacpFlag;
                    char *name;

                    FOREACH_LC(n,normalAttrNamesCopy)
                    {
                        nacpFlag = FALSE;
                        FORBOTH_LC(lc1, lc2,((ProjectionOperator *)o)->projExprs,o->schema->attrDefs)
                        {
                            if(streq(((AttributeReference *)LC_P_VAL(lc1))->name,LC_P_VAL(n)))
                            {
                                name = ((AttributeDef *)LC_P_VAL(lc2))->attrName;
                                nacpFlag = TRUE;
                                break;
                            }
                        }
                        if(nacpFlag == TRUE)
                            normalAttrNamesCopyTempList = appendToTailOfList(normalAttrNamesCopyTempList, name);
                        else
                            normalAttrNamesCopyTempList = appendToTailOfList(normalAttrNamesCopyTempList, n);
                    }
                    normalAttrNamesCopy = normalAttrNamesCopyTempList;
                }
                //Just get rid of the attrDef from schema
		deleteAttrFromSchemaByName((QueryOperator *)o, LC_P_VAL(d));
            }
        }

        if(isLost)
        {
            List* projExpr = NIL;
            List *provAttr = NIL;

            //Create the new schema attrDef names in provAttr list
            FOREACH(AttributeReference, attrProv, ((ProjectionOperator *)o)->projExprs)
	    {
                provAttr = appendToTailOfList(provAttr, attrProv->name);
            }

            //Create the attr reference from upper op projExprs
            int cnt = 0;
            FOREACH_LC(lc, ((ProjectionOperator *)o)->projExprs)
            {
                projExpr = appendToTailOfList(projExpr,
                                              createFullAttrReference(
                                                        ((AttributeDef *) LC_P_VAL(lc))->attrName, 0,
                                                        cnt, 0,
                                                        ((AttributeDef *) LC_P_VAL(lc))->dataType));
                cnt++;
            }

            //Change the attr reference
	    FORBOTH_LC(attrProvName, attrNorName, LostList, LostNormalList)
            {
                FOREACH(AttributeReference, p, projExpr)
                {
                    if(streq(p->name,LC_P_VAL(attrProvName)))
                    {
                        p->name = LC_P_VAL(attrNorName);
                        break;
                    }
                }
            }

            List *newProvPosList = NIL;
            CREATE_INT_SEQ(newProvPosList, cnt, (cnt * 2) - 1, 1);

            //Add projection
            ProjectionOperator *newpo = createProjectionOp(projExpr, NULL, NIL, provAttr);
	    newpo->op.provAttrs = newProvPosList;

            // Switch the subtree with this newly created projection operator.
            switchSubtrees((QueryOperator *) op, (QueryOperator *) newpo);

            // Add child to the newly created projections operator,
            addChildOperator((QueryOperator *) newpo, (QueryOperator *) op);

            //Reset the pos of the schema
            resetPosOfAttrRefBaseOnBelowLayerSchema((ProjectionOperator *)newpo,(QueryOperator *)op);
	    resetPosOfAttrRefBaseOnBelowLayerSchema((ProjectionOperator *)o,(QueryOperator *)newpo);

            pullup(o, duplicateattrsCopy, normalAttrNamesCopy);
        }
        else
        {
            pullup(o, duplicateattrsCopy, normalAttrNamesCopy);
        }
    }

    return op;
}
