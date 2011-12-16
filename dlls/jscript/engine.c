/*
 * Copyright 2008 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <math.h>
#include <assert.h>

#include "jscript.h"
#include "engine.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(jscript);

#define EXPR_NOVAL   0x0001

static const WCHAR booleanW[] = {'b','o','o','l','e','a','n',0};
static const WCHAR functionW[] = {'f','u','n','c','t','i','o','n',0};
static const WCHAR numberW[] = {'n','u','m','b','e','r',0};
static const WCHAR objectW[] = {'o','b','j','e','c','t',0};
static const WCHAR stringW[] = {'s','t','r','i','n','g',0};
static const WCHAR undefinedW[] = {'u','n','d','e','f','i','n','e','d',0};
static const WCHAR unknownW[] = {'u','n','k','n','o','w','n',0};

struct _return_type_t {
    enum{
        RT_NORMAL,
        RT_RETURN,
        RT_BREAK,
        RT_CONTINUE
    } type;
    jsexcept_t ei;
};

static inline HRESULT stat_eval(script_ctx_t *ctx, statement_t *stat, return_type_t *rt, VARIANT *ret)
{
    return stat->eval(ctx, stat, rt, ret);
}

static HRESULT expr_eval(script_ctx_t*,expression_t*,DWORD,jsexcept_t*,exprval_t*);

static HRESULT stack_push(exec_ctx_t *ctx, VARIANT *v)
{
    if(!ctx->stack_size) {
        ctx->stack = heap_alloc(16*sizeof(VARIANT));
        if(!ctx->stack)
            return E_OUTOFMEMORY;
        ctx->stack_size = 16;
    }else if(ctx->stack_size == ctx->top) {
        VARIANT *new_stack;

        new_stack = heap_realloc(ctx->stack, ctx->stack_size*2*sizeof(VARIANT));
        if(!new_stack) {
            VariantClear(v);
            return E_OUTOFMEMORY;
        }

        ctx->stack = new_stack;
        ctx->stack_size *= 2;
    }

    ctx->stack[ctx->top++] = *v;
    return S_OK;
}

static HRESULT stack_push_bool(exec_ctx_t *ctx, BOOL b)
{
    VARIANT v;

    V_VT(&v) = VT_BOOL;
    V_BOOL(&v) = b ? VARIANT_TRUE : VARIANT_FALSE;
    return stack_push(ctx, &v);
}

static inline HRESULT stack_push_number(exec_ctx_t *ctx, double number)
{
    VARIANT v;

    num_set_val(&v, number);
    return stack_push(ctx, &v);
}

static inline HRESULT stack_push_int(exec_ctx_t *ctx, INT n)
{
    VARIANT v;

    V_VT(&v) = VT_I4;
    V_I4(&v) = n;
    return stack_push(ctx, &v);
}

static inline HRESULT stack_push_string(exec_ctx_t *ctx, const WCHAR *str)
{
    VARIANT v;

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(str);
    return V_BSTR(&v) ? stack_push(ctx, &v) : E_OUTOFMEMORY;
}

static HRESULT stack_push_objid(exec_ctx_t *ctx, IDispatch *disp, DISPID id)
{
    VARIANT v;
    HRESULT hres;

    V_VT(&v) = VT_DISPATCH;
    V_DISPATCH(&v) = disp;
    hres = stack_push(ctx, &v);
    if(FAILED(hres))
        return hres;

    V_VT(&v) = VT_INT;
    V_INT(&v) = id;
    return stack_push(ctx, &v);
}

static inline VARIANT *stack_top(exec_ctx_t *ctx)
{
    assert(ctx->top);
    return ctx->stack + ctx->top-1;
}

static inline VARIANT *stack_topn(exec_ctx_t *ctx, unsigned n)
{
    assert(ctx->top > n);
    return ctx->stack + ctx->top-1-n;
}

static inline VARIANT *stack_pop(exec_ctx_t *ctx)
{
    assert(ctx->top);
    return ctx->stack + --ctx->top;
}

static void stack_popn(exec_ctx_t *ctx, unsigned n)
{
    while(n--)
        VariantClear(stack_pop(ctx));
}

static HRESULT stack_pop_number(exec_ctx_t *ctx, VARIANT *r)
{
    VARIANT *v;
    HRESULT hres;

    v = stack_pop(ctx);
    hres = to_number(ctx->parser->script, v, &ctx->ei, r);
    VariantClear(v);
    return hres;
}

static HRESULT stack_pop_object(exec_ctx_t *ctx, IDispatch **r)
{
    VARIANT *v;
    HRESULT hres;

    v = stack_pop(ctx);
    if(V_VT(v) == VT_DISPATCH) {
        if(!V_DISPATCH(v))
            return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_OBJECT_REQUIRED, NULL);
        *r = V_DISPATCH(v);
        return S_OK;
    }

    hres = to_object(ctx->parser->script, v, r);
    VariantClear(v);
    return hres;
}

static inline HRESULT stack_pop_int(exec_ctx_t *ctx, INT *r)
{
    return to_int32(ctx->parser->script, stack_pop(ctx), &ctx->ei, r);
}

static inline HRESULT stack_pop_uint(exec_ctx_t *ctx, DWORD *r)
{
    return to_uint32(ctx->parser->script, stack_pop(ctx), &ctx->ei, r);
}

static inline IDispatch *stack_pop_objid(exec_ctx_t *ctx, DISPID *id)
{
    assert(V_VT(stack_top(ctx)) == VT_INT && V_VT(stack_topn(ctx, 1)) == VT_DISPATCH);

    *id = V_INT(stack_pop(ctx));
    return V_DISPATCH(stack_pop(ctx));
}

static inline IDispatch *stack_topn_objid(exec_ctx_t *ctx, unsigned n, DISPID *id)
{
    assert(V_VT(stack_topn(ctx, n)) == VT_INT && V_VT(stack_topn(ctx, n+1)) == VT_DISPATCH);

    *id = V_INT(stack_topn(ctx, n));
    return V_DISPATCH(stack_topn(ctx, n+1));
}

static void exprval_release(exprval_t *val)
{
    switch(val->type) {
    case EXPRVAL_VARIANT:
        if(V_VT(&val->u.var) != VT_EMPTY)
            VariantClear(&val->u.var);
        return;
    case EXPRVAL_IDREF:
        if(val->u.idref.disp)
            IDispatch_Release(val->u.idref.disp);
        return;
    case EXPRVAL_INVALID:
        SysFreeString(val->u.identifier);
    }
}

/* ECMA-262 3rd Edition    8.7.1 */
static HRESULT exprval_value(script_ctx_t *ctx, exprval_t *val, jsexcept_t *ei, VARIANT *ret)
{
    V_VT(ret) = VT_EMPTY;

    switch(val->type) {
    case EXPRVAL_VARIANT:
        return VariantCopy(ret, &val->u.var);
    case EXPRVAL_IDREF:
        if(!val->u.idref.disp) {
            FIXME("throw ReferenceError\n");
            return E_FAIL;
        }

        return disp_propget(ctx, val->u.idref.disp, val->u.idref.id, ret, ei, NULL/*FIXME*/);
    case EXPRVAL_INVALID:
        return throw_type_error(ctx, ei, JS_E_UNDEFINED_VARIABLE, val->u.identifier);
    }

    ERR("type %d\n", val->type);
    return E_FAIL;
}

static HRESULT exprval_to_value(script_ctx_t *ctx, exprval_t *val, jsexcept_t *ei, VARIANT *ret)
{
    if(val->type == EXPRVAL_VARIANT) {
        *ret = val->u.var;
        V_VT(&val->u.var) = VT_EMPTY;
        return S_OK;
    }

    return exprval_value(ctx, val, ei, ret);
}

static HRESULT exprval_to_boolean(script_ctx_t *ctx, exprval_t *exprval, jsexcept_t *ei, VARIANT_BOOL *b)
{
    if(exprval->type != EXPRVAL_VARIANT) {
        VARIANT val;
        HRESULT hres;

        hres = exprval_to_value(ctx, exprval, ei, &val);
        if(FAILED(hres))
            return hres;

        hres = to_boolean(&val, b);
        VariantClear(&val);
        return hres;
    }

    return to_boolean(&exprval->u.var, b);
}

static void exprval_set_idref(exprval_t *val, IDispatch *disp, DISPID id)
{
    val->type = EXPRVAL_IDREF;
    val->u.idref.disp = disp;
    val->u.idref.id = id;

    if(disp)
        IDispatch_AddRef(disp);
}

HRESULT scope_push(scope_chain_t *scope, jsdisp_t *obj, scope_chain_t **ret)
{
    scope_chain_t *new_scope;

    new_scope = heap_alloc(sizeof(scope_chain_t));
    if(!new_scope)
        return E_OUTOFMEMORY;

    new_scope->ref = 1;

    jsdisp_addref(obj);
    new_scope->obj = obj;

    if(scope) {
        scope_addref(scope);
        new_scope->next = scope;
    }else {
        new_scope->next = NULL;
    }

    *ret = new_scope;
    return S_OK;
}

static void scope_pop(scope_chain_t **scope)
{
    scope_chain_t *tmp;

    tmp = *scope;
    *scope = tmp->next;
    scope_release(tmp);
}

void scope_release(scope_chain_t *scope)
{
    if(--scope->ref)
        return;

    if(scope->next)
        scope_release(scope->next);

    jsdisp_release(scope->obj);
    heap_free(scope);
}

HRESULT create_exec_ctx(script_ctx_t *script_ctx, IDispatch *this_obj, jsdisp_t *var_disp,
        scope_chain_t *scope, BOOL is_global, exec_ctx_t **ret)
{
    exec_ctx_t *ctx;

    ctx = heap_alloc_zero(sizeof(exec_ctx_t));
    if(!ctx)
        return E_OUTOFMEMORY;

    ctx->ref = 1;
    ctx->is_global = is_global;

    if(this_obj)
        ctx->this_obj = this_obj;
    else if(script_ctx->host_global)
        ctx->this_obj = script_ctx->host_global;
    else
        ctx->this_obj = to_disp(script_ctx->global);
    IDispatch_AddRef(ctx->this_obj);

    jsdisp_addref(var_disp);
    ctx->var_disp = var_disp;

    if(scope) {
        scope_addref(scope);
        ctx->scope_chain = scope;
    }

    *ret = ctx;
    return S_OK;
}

void exec_release(exec_ctx_t *ctx)
{
    if(--ctx->ref)
        return;

    if(ctx->scope_chain)
        scope_release(ctx->scope_chain);
    if(ctx->var_disp)
        jsdisp_release(ctx->var_disp);
    if(ctx->this_obj)
        IDispatch_Release(ctx->this_obj);
    heap_free(ctx->stack);
    heap_free(ctx);
}

static HRESULT disp_get_id(script_ctx_t *ctx, IDispatch *disp, BSTR name, DWORD flags, DISPID *id)
{
    IDispatchEx *dispex;
    HRESULT hres;

    hres = IDispatch_QueryInterface(disp, &IID_IDispatchEx, (void**)&dispex);
    if(FAILED(hres)) {
        TRACE("unsing IDispatch\n");

        *id = 0;
        return IDispatch_GetIDsOfNames(disp, &IID_NULL, &name, 1, 0, id);
    }

    *id = 0;
    hres = IDispatchEx_GetDispID(dispex, name, make_grfdex(ctx, flags|fdexNameCaseSensitive), id);
    IDispatchEx_Release(dispex);
    return hres;
}

/* ECMA-262 3rd Edition    8.7.2 */
static HRESULT put_value(script_ctx_t *ctx, exprval_t *ref, VARIANT *v, jsexcept_t *ei)
{
    if(ref->type != EXPRVAL_IDREF)
        return throw_reference_error(ctx, ei, JS_E_ILLEGAL_ASSIGN, NULL);

    return disp_propput(ctx, ref->u.idref.disp, ref->u.idref.id, v, ei, NULL/*FIXME*/);
}

static inline BOOL is_null(const VARIANT *v)
{
    return V_VT(v) == VT_NULL || (V_VT(v) == VT_DISPATCH && !V_DISPATCH(v));
}

static HRESULT disp_cmp(IDispatch *disp1, IDispatch *disp2, BOOL *ret)
{
    IObjectIdentity *identity;
    IUnknown *unk1, *unk2;
    HRESULT hres;

    if(disp1 == disp2) {
        *ret = TRUE;
        return S_OK;
    }

    if(!disp1 || !disp2) {
        *ret = FALSE;
        return S_OK;
    }

    hres = IDispatch_QueryInterface(disp1, &IID_IUnknown, (void**)&unk1);
    if(FAILED(hres))
        return hres;

    hres = IDispatch_QueryInterface(disp2, &IID_IUnknown, (void**)&unk2);
    if(FAILED(hres)) {
        IUnknown_Release(unk1);
        return hres;
    }

    if(unk1 == unk2) {
        *ret = TRUE;
    }else {
        hres = IUnknown_QueryInterface(unk1, &IID_IObjectIdentity, (void**)&identity);
        if(SUCCEEDED(hres)) {
            hres = IObjectIdentity_IsEqualObject(identity, unk2);
            IObjectIdentity_Release(identity);
            *ret = hres == S_OK;
        }else {
            *ret = FALSE;
        }
    }

    IUnknown_Release(unk1);
    IUnknown_Release(unk2);
    return S_OK;
}

/* ECMA-262 3rd Edition    11.9.6 */
static HRESULT equal2_values(VARIANT *lval, VARIANT *rval, BOOL *ret)
{
    TRACE("\n");

    if(V_VT(lval) != V_VT(rval)) {
        if(is_num_vt(V_VT(lval)) && is_num_vt(V_VT(rval)))
            *ret = num_val(lval) == num_val(rval);
        else if(is_null(lval))
            *ret = is_null(rval);
        else
            *ret = FALSE;
        return S_OK;
    }

    switch(V_VT(lval)) {
    case VT_EMPTY:
    case VT_NULL:
        *ret = VARIANT_TRUE;
        break;
    case VT_I4:
        *ret = V_I4(lval) == V_I4(rval);
        break;
    case VT_R8:
        *ret = V_R8(lval) == V_R8(rval);
        break;
    case VT_BSTR:
        if(!V_BSTR(lval))
            *ret = SysStringLen(V_BSTR(rval))?FALSE:TRUE;
        else if(!V_BSTR(rval))
            *ret = SysStringLen(V_BSTR(lval))?FALSE:TRUE;
        else
            *ret = !strcmpW(V_BSTR(lval), V_BSTR(rval));
        break;
    case VT_DISPATCH:
        return disp_cmp(V_DISPATCH(lval), V_DISPATCH(rval), ret);
    case VT_BOOL:
        *ret = !V_BOOL(lval) == !V_BOOL(rval);
        break;
    default:
        FIXME("unimplemented vt %d\n", V_VT(lval));
        return E_NOTIMPL;
    }

    return S_OK;
}

static BOOL lookup_global_members(script_ctx_t *ctx, BSTR identifier, exprval_t *ret)
{
    named_item_t *item;
    DISPID id;
    HRESULT hres;

    for(item = ctx->named_items; item; item = item->next) {
        if(item->flags & SCRIPTITEM_GLOBALMEMBERS) {
            hres = disp_get_id(ctx, item->disp, identifier, 0, &id);
            if(SUCCEEDED(hres)) {
                if(ret)
                    exprval_set_idref(ret, item->disp, id);
                return TRUE;
            }
        }
    }

    return FALSE;
}

HRESULT exec_source(exec_ctx_t *ctx, parser_ctx_t *parser, source_elements_t *source, BOOL from_eval,
        jsexcept_t *ei, VARIANT *retv)
{
    script_ctx_t *script = parser->script;
    function_declaration_t *func;
    parser_ctx_t *prev_parser;
    var_list_t *var;
    VARIANT val, tmp;
    statement_t *stat;
    exec_ctx_t *prev_ctx;
    return_type_t rt;
    HRESULT hres = S_OK;

    for(func = source->functions; func; func = func->next) {
        jsdisp_t *func_obj;
        VARIANT var;

        hres = create_source_function(parser, func->expr->parameter_list, func->expr->source_elements,
                ctx->scope_chain, func->expr->src_str, func->expr->src_len, &func_obj);
        if(FAILED(hres))
            return hres;

        var_set_jsdisp(&var, func_obj);
        hres = jsdisp_propput_name(ctx->var_disp, func->expr->identifier, &var, ei, NULL);
        jsdisp_release(func_obj);
        if(FAILED(hres))
            return hres;
    }

    for(var = source->variables; var; var = var->next) {
        DISPID id = 0;
        BSTR name;

        name = SysAllocString(var->identifier);
        if(!name)
            return E_OUTOFMEMORY;

        if(!ctx->is_global || !lookup_global_members(parser->script, name, NULL))
            hres = jsdisp_get_id(ctx->var_disp, var->identifier, fdexNameEnsure, &id);
        SysFreeString(name);
        if(FAILED(hres))
            return hres;
    }

    prev_ctx = script->exec_ctx;
    script->exec_ctx = ctx;

    prev_parser = ctx->parser;
    ctx->parser = parser;

    V_VT(&val) = VT_EMPTY;
    memset(&rt, 0, sizeof(rt));
    rt.type = RT_NORMAL;

    for(stat = source->statement; stat; stat = stat->next) {
        hres = stat_eval(script, stat, &rt, &tmp);
        if(FAILED(hres))
            break;

        VariantClear(&val);
        val = tmp;
        if(rt.type != RT_NORMAL)
            break;
    }

    script->exec_ctx = prev_ctx;
    ctx->parser = prev_parser;

    if(rt.type != RT_NORMAL && rt.type != RT_RETURN) {
        FIXME("wrong rt %d\n", rt.type);
        hres = E_FAIL;
    }

    *ei = rt.ei;
    if(FAILED(hres)) {
        VariantClear(&val);
        return hres;
    }

    if(!retv || (!from_eval && rt.type != RT_RETURN))
        VariantClear(&val);
    if(retv)
        *retv = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    10.1.4 */
static HRESULT identifier_eval(script_ctx_t *ctx, BSTR identifier, DWORD flags, jsexcept_t *ei, exprval_t *ret)
{
    scope_chain_t *scope;
    named_item_t *item;
    DISPID id = 0;
    HRESULT hres;

    TRACE("%s\n", debugstr_w(identifier));

    for(scope = ctx->exec_ctx->scope_chain; scope; scope = scope->next) {
        hres = jsdisp_get_id(scope->obj, identifier, 0, &id);
        if(SUCCEEDED(hres)) {
            exprval_set_idref(ret, to_disp(scope->obj), id);
            return S_OK;
        }
    }

    hres = jsdisp_get_id(ctx->global, identifier, 0, &id);
    if(SUCCEEDED(hres)) {
        exprval_set_idref(ret, to_disp(ctx->global), id);
        return S_OK;
    }

    for(item = ctx->named_items; item; item = item->next) {
        if((item->flags & SCRIPTITEM_ISVISIBLE) && !strcmpW(item->name, identifier)) {
            if(!item->disp) {
                IUnknown *unk;

                if(!ctx->site)
                    break;

                hres = IActiveScriptSite_GetItemInfo(ctx->site, identifier,
                                                     SCRIPTINFO_IUNKNOWN, &unk, NULL);
                if(FAILED(hres)) {
                    WARN("GetItemInfo failed: %08x\n", hres);
                    break;
                }

                hres = IUnknown_QueryInterface(unk, &IID_IDispatch, (void**)&item->disp);
                IUnknown_Release(unk);
                if(FAILED(hres)) {
                    WARN("object does not implement IDispatch\n");
                    break;
                }
            }

            ret->type = EXPRVAL_VARIANT;
            V_VT(&ret->u.var) = VT_DISPATCH;
            V_DISPATCH(&ret->u.var) = item->disp;
            IDispatch_AddRef(item->disp);
            return S_OK;
        }
    }

    if(lookup_global_members(ctx, identifier, ret))
        return S_OK;

    if(flags & fdexNameEnsure) {
        hres = jsdisp_get_id(ctx->global, identifier, fdexNameEnsure, &id);
        if(FAILED(hres))
            return hres;

        exprval_set_idref(ret, to_disp(ctx->global), id);
        return S_OK;
    }

    ret->type = EXPRVAL_INVALID;
    ret->u.identifier = SysAllocString(identifier);
    if(!ret->u.identifier)
        return E_OUTOFMEMORY;

    return S_OK;
}

/* ECMA-262 3rd Edition    12.1 */
HRESULT block_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    block_statement_t *stat = (block_statement_t*)_stat;
    VARIANT val, tmp;
    statement_t *iter;
    HRESULT hres = S_OK;

    TRACE("\n");

    V_VT(&val) = VT_EMPTY;
    for(iter = stat->stat_list; iter; iter = iter->next) {
        hres = stat_eval(ctx, iter, rt, &tmp);
        if(FAILED(hres))
            break;

        VariantClear(&val);
        val = tmp;
        if(rt->type != RT_NORMAL)
            break;
    }

    if(FAILED(hres)) {
        VariantClear(&val);
        return hres;
    }

    *ret = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.2 */
static HRESULT variable_list_eval(script_ctx_t *ctx, variable_declaration_t *var_list, jsexcept_t *ei)
{
    variable_declaration_t *iter;
    HRESULT hres = S_OK;

    for(iter = var_list; iter; iter = iter->next) {
        exprval_t exprval;
        VARIANT val;

        if(!iter->expr)
            continue;

        hres = expr_eval(ctx, iter->expr, 0, ei, &exprval);
        if(FAILED(hres))
            break;

        hres = exprval_to_value(ctx, &exprval, ei, &val);
        exprval_release(&exprval);
        if(FAILED(hres))
            break;

        hres = jsdisp_propput_name(ctx->exec_ctx->var_disp, iter->identifier, &val, ei, NULL/*FIXME*/);
        VariantClear(&val);
        if(FAILED(hres))
            break;
    }

    return hres;
}

/* ECMA-262 3rd Edition    12.2 */
HRESULT var_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    var_statement_t *stat = (var_statement_t*)_stat;
    HRESULT hres;

    TRACE("\n");

    hres = variable_list_eval(ctx, stat->variable_list, &rt->ei);
    if(FAILED(hres))
        return hres;

    V_VT(ret) = VT_EMPTY;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.3 */
HRESULT empty_statement_eval(script_ctx_t *ctx, statement_t *stat, return_type_t *rt, VARIANT *ret)
{
    TRACE("\n");

    V_VT(ret) = VT_EMPTY;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.4 */
HRESULT expression_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    expression_statement_t *stat = (expression_statement_t*)_stat;
    exprval_t exprval;
    VARIANT val;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, stat->expr, EXPR_NOVAL, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    *ret = val;
    TRACE("= %s\n", debugstr_variant(ret));
    return S_OK;
}

/* ECMA-262 3rd Edition    12.5 */
HRESULT if_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    if_statement_t *stat = (if_statement_t*)_stat;
    exprval_t exprval;
    VARIANT_BOOL b;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_boolean(ctx, &exprval, &rt->ei, &b);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    if(b)
        hres = stat_eval(ctx, stat->if_stat, rt, ret);
    else if(stat->else_stat)
        hres = stat_eval(ctx, stat->else_stat, rt, ret);
    else
        V_VT(ret) = VT_EMPTY;

    return hres;
}

/* ECMA-262 3rd Edition    12.6.2 */
HRESULT while_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    while_statement_t *stat = (while_statement_t*)_stat;
    exprval_t exprval;
    VARIANT val, tmp;
    VARIANT_BOOL b;
    BOOL test_expr;
    HRESULT hres;

    TRACE("\n");

    V_VT(&val) = VT_EMPTY;
    test_expr = !stat->do_while;

    while(1) {
        if(test_expr) {
            hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
            if(FAILED(hres))
                break;

            hres = exprval_to_boolean(ctx, &exprval, &rt->ei, &b);
            exprval_release(&exprval);
            if(FAILED(hres) || !b)
                break;
        }else {
            test_expr = TRUE;
        }

        hres = stat_eval(ctx, stat->statement, rt, &tmp);
        if(FAILED(hres))
            break;

        VariantClear(&val);
        val = tmp;

        if(rt->type == RT_CONTINUE)
            rt->type = RT_NORMAL;
        if(rt->type != RT_NORMAL)
            break;
    }

    if(FAILED(hres)) {
        VariantClear(&val);
        return hres;
    }

    if(rt->type == RT_BREAK)
        rt->type = RT_NORMAL;

    *ret = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.6.3 */
HRESULT for_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    for_statement_t *stat = (for_statement_t*)_stat;
    VARIANT val, tmp, retv;
    exprval_t exprval;
    VARIANT_BOOL b;
    HRESULT hres;

    TRACE("\n");

    if(stat->variable_list) {
        hres = variable_list_eval(ctx, stat->variable_list, &rt->ei);
        if(FAILED(hres))
            return hres;
    }else if(stat->begin_expr) {
        hres = expr_eval(ctx, stat->begin_expr, 0, &rt->ei, &exprval);
        if(FAILED(hres))
            return hres;

        hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
        exprval_release(&exprval);
        if(FAILED(hres))
            return hres;

        VariantClear(&val);
    }

    V_VT(&retv) = VT_EMPTY;

    while(1) {
        if(stat->expr) {
            hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
            if(FAILED(hres))
                break;

            hres = exprval_to_boolean(ctx, &exprval, &rt->ei, &b);
            exprval_release(&exprval);
            if(FAILED(hres) || !b)
                break;
        }

        hres = stat_eval(ctx, stat->statement, rt, &tmp);
        if(FAILED(hres))
            break;

        VariantClear(&retv);
        retv = tmp;

        if(rt->type == RT_CONTINUE)
            rt->type = RT_NORMAL;
        else if(rt->type != RT_NORMAL)
            break;

        if(stat->end_expr) {
            hres = expr_eval(ctx, stat->end_expr, 0, &rt->ei, &exprval);
            if(FAILED(hres))
                break;

            hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
            exprval_release(&exprval);
            if(FAILED(hres))
                break;

            VariantClear(&val);
        }
    }

    if(FAILED(hres)) {
        VariantClear(&retv);
        return hres;
    }

    if(rt->type == RT_BREAK)
        rt->type = RT_NORMAL;

    *ret = retv;
    return S_OK;
}

static HRESULT array_expression_eval(script_ctx_t*,expression_t*,jsexcept_t*,exprval_t*);
static HRESULT member_expression_eval(script_ctx_t*,expression_t*,jsexcept_t*,exprval_t*);
static HRESULT identifier_expression_eval(script_ctx_t*,expression_t*,jsexcept_t*,exprval_t*);

/* ECMA-262 3rd Edition    12.6.4 */
HRESULT forin_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    forin_statement_t *stat = (forin_statement_t*)_stat;
    VARIANT val, name, retv, tmp;
    DISPID id = DISPID_STARTENUM;
    BSTR str, identifier = NULL;
    IDispatchEx *in_obj;
    exprval_t exprval;
    HRESULT hres;

    TRACE("\n");

    if(stat->variable) {
        hres = variable_list_eval(ctx, stat->variable, &rt->ei);
        if(FAILED(hres))
            return hres;
    }

    hres = expr_eval(ctx, stat->in_expr, 0, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    if(V_VT(&val) != VT_DISPATCH) {
        TRACE("in vt %d\n", V_VT(&val));
        VariantClear(&val);
        V_VT(ret) = VT_EMPTY;
        return S_OK;
    }

    hres = IDispatch_QueryInterface(V_DISPATCH(&val), &IID_IDispatchEx, (void**)&in_obj);
    IDispatch_Release(V_DISPATCH(&val));
    if(FAILED(hres)) {
        TRACE("Object doesn't support IDispatchEx\n");
        V_VT(ret) = VT_EMPTY;
        return S_OK;
    }

    V_VT(&retv) = VT_EMPTY;

    if(stat->variable)
        identifier = SysAllocString(stat->variable->identifier);

    while(1) {
        hres = IDispatchEx_GetNextDispID(in_obj, fdexEnumDefault, id, &id);
        if(FAILED(hres) || hres == S_FALSE)
            break;

        hres = IDispatchEx_GetMemberName(in_obj, id, &str);
        if(FAILED(hres))
            break;

        TRACE("iter %s\n", debugstr_w(str));

        if(stat->variable) {
            hres = identifier_eval(ctx, identifier, 0, NULL, &exprval);
        }else {
            switch(stat->expr->type) {
            case EXPR_ARRAY:
                hres = array_expression_eval(ctx, stat->expr, &rt->ei, &exprval);
                break;
            case EXPR_IDENT:
                hres = identifier_expression_eval(ctx, stat->expr, &rt->ei, &exprval);
                break;
            case EXPR_MEMBER:
                hres = member_expression_eval(ctx, stat->expr, &rt->ei, &exprval);
                break;
            default:
                hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
            }
        }
        if(SUCCEEDED(hres)) {
            V_VT(&name) = VT_BSTR;
            V_BSTR(&name) = str;
            hres = put_value(ctx, &exprval, &name, &rt->ei);
            exprval_release(&exprval);
        }
        SysFreeString(str);
        if(FAILED(hres))
            break;

        hres = stat_eval(ctx, stat->statement, rt, &tmp);
        if(FAILED(hres))
            break;

        VariantClear(&retv);
        retv = tmp;

        if(rt->type == RT_CONTINUE)
            rt->type = RT_NORMAL;
        else if(rt->type != RT_NORMAL)
            break;
    }

    SysFreeString(identifier);
    IDispatchEx_Release(in_obj);
    if(FAILED(hres)) {
        VariantClear(&retv);
        return hres;
    }

    if(rt->type == RT_BREAK)
        rt->type = RT_NORMAL;

    *ret = retv;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.7 */
HRESULT continue_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    branch_statement_t *stat = (branch_statement_t*)_stat;

    TRACE("\n");

    if(stat->identifier) {
        FIXME("indentifier not implemented\n");
        return E_NOTIMPL;
    }

    rt->type = RT_CONTINUE;
    V_VT(ret) = VT_EMPTY;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.8 */
HRESULT break_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    branch_statement_t *stat = (branch_statement_t*)_stat;

    TRACE("\n");

    if(stat->identifier) {
        FIXME("indentifier not implemented\n");
        return E_NOTIMPL;
    }

    rt->type = RT_BREAK;
    V_VT(ret) = VT_EMPTY;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.9 */
HRESULT return_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    expression_statement_t *stat = (expression_statement_t*)_stat;
    HRESULT hres;

    TRACE("\n");

    if(stat->expr) {
        exprval_t exprval;

        hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
        if(FAILED(hres))
            return hres;

        hres = exprval_to_value(ctx, &exprval, &rt->ei, ret);
        exprval_release(&exprval);
        if(FAILED(hres))
            return hres;
    }else {
        V_VT(ret) = VT_EMPTY;
    }

    TRACE("= %s\n", debugstr_variant(ret));
    rt->type = RT_RETURN;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.10 */
HRESULT with_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    with_statement_t *stat = (with_statement_t*)_stat;
    exprval_t exprval;
    IDispatch *disp;
    jsdisp_t *obj;
    VARIANT val;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    hres = to_object(ctx, &val, &disp);
    VariantClear(&val);
    if(FAILED(hres))
        return hres;

    obj = iface_to_jsdisp((IUnknown*)disp);
    IDispatch_Release(disp);
    if(!obj) {
        FIXME("disp id not jsdisp\n");
        return E_NOTIMPL;
    }

    hres = scope_push(ctx->exec_ctx->scope_chain, obj, &ctx->exec_ctx->scope_chain);
    jsdisp_release(obj);
    if(FAILED(hres))
        return hres;

    hres = stat_eval(ctx, stat->statement, rt, ret);

    scope_pop(&ctx->exec_ctx->scope_chain);
    return hres;
}

/* ECMA-262 3rd Edition    12.12 */
HRESULT labelled_statement_eval(script_ctx_t *ctx, statement_t *stat, return_type_t *rt, VARIANT *ret)
{
    FIXME("\n");
    return E_NOTIMPL;
}

/* ECMA-262 3rd Edition    12.13 */
HRESULT switch_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    switch_statement_t *stat = (switch_statement_t*)_stat;
    case_clausule_t *iter, *default_clausule = NULL;
    statement_t *stat_iter;
    VARIANT val, cval;
    exprval_t exprval;
    BOOL b;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    for(iter = stat->case_list; iter; iter = iter->next) {
        if(!iter->expr) {
            default_clausule = iter;
            continue;
        }

        hres = expr_eval(ctx, iter->expr, 0, &rt->ei, &exprval);
        if(FAILED(hres))
            break;

        hres = exprval_to_value(ctx, &exprval, &rt->ei, &cval);
        exprval_release(&exprval);
        if(FAILED(hres))
            break;

        hres = equal2_values(&val, &cval, &b);
        VariantClear(&cval);
        if(FAILED(hres) || b)
            break;
    }

    VariantClear(&val);
    if(FAILED(hres))
        return hres;

    if(!iter)
        iter = default_clausule;

    V_VT(&val) = VT_EMPTY;
    if(iter) {
        VARIANT tmp;

        for(stat_iter = iter->stat; stat_iter; stat_iter = stat_iter->next) {
            hres = stat_eval(ctx, stat_iter, rt, &tmp);
            if(FAILED(hres))
                break;

            VariantClear(&val);
            val = tmp;

            if(rt->type != RT_NORMAL)
                break;
        }
    }

    if(FAILED(hres)) {
        VariantClear(&val);
        return hres;
    }

    if(rt->type == RT_BREAK)
        rt->type = RT_NORMAL;

    *ret = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.13 */
HRESULT throw_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    expression_statement_t *stat = (expression_statement_t*)_stat;
    exprval_t exprval;
    VARIANT val;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, stat->expr, 0, &rt->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, &rt->ei, &val);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    rt->ei.var = val;
    return DISP_E_EXCEPTION;
}

static HRESULT interp_throw(exec_ctx_t *ctx)
{
    const HRESULT arg = ctx->parser->code->instrs[ctx->ip].arg1.uint;

    TRACE("%08x\n", arg);

    return throw_reference_error(ctx->parser->script, &ctx->ei, arg, NULL);
}

static HRESULT interp_throw_type(exec_ctx_t *ctx)
{
    const HRESULT hres = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    const WCHAR *str = ctx->parser->code->instrs[ctx->ip].arg2.str;

    TRACE("%08x %s\n", hres, debugstr_w(str));

    return throw_type_error(ctx->parser->script, &ctx->ei, hres, str);
}

/* ECMA-262 3rd Edition    12.14 */
static HRESULT catch_eval(script_ctx_t *ctx, catch_block_t *block, return_type_t *rt, VARIANT *ret)
{
    jsdisp_t *var_disp;
    VARIANT ex, val;
    HRESULT hres;

    ex = rt->ei.var;
    memset(&rt->ei, 0, sizeof(jsexcept_t));

    hres = create_dispex(ctx, NULL, NULL, &var_disp);
    if(SUCCEEDED(hres)) {
        hres = jsdisp_propput_name(var_disp, block->identifier, &ex, &rt->ei, NULL/*FIXME*/);
        if(SUCCEEDED(hres)) {
            hres = scope_push(ctx->exec_ctx->scope_chain, var_disp, &ctx->exec_ctx->scope_chain);
            if(SUCCEEDED(hres)) {
                hres = stat_eval(ctx, block->statement, rt, &val);
                scope_pop(&ctx->exec_ctx->scope_chain);
            }
        }

        jsdisp_release(var_disp);
    }

    VariantClear(&ex);
    if(FAILED(hres))
        return hres;

    *ret = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    12.14 */
HRESULT try_statement_eval(script_ctx_t *ctx, statement_t *_stat, return_type_t *rt, VARIANT *ret)
{
    try_statement_t *stat = (try_statement_t*)_stat;
    VARIANT val;
    HRESULT hres;

    TRACE("\n");

    hres = stat_eval(ctx, stat->try_statement, rt, &val);
    if(FAILED(hres)) {
        TRACE("EXCEPTION\n");
        if(!stat->catch_block)
            return hres;

        hres = catch_eval(ctx, stat->catch_block, rt, &val);
        if(FAILED(hres))
            return hres;
    }

    if(stat->finally_statement) {
        VariantClear(&val);
        hres = stat_eval(ctx, stat->finally_statement, rt, &val);
        if(FAILED(hres))
            return hres;
    }

    *ret = val;
    return S_OK;
}

/* ECMA-262 3rd Edition    13 */
static HRESULT interp_func(exec_ctx_t *ctx)
{
    function_expression_t *expr = ctx->parser->code->instrs[ctx->ip].arg1.func;
    jsdisp_t *dispex;
    VARIANT v;
    HRESULT hres;

    TRACE("\n");

    hres = create_source_function(ctx->parser, expr->parameter_list, expr->source_elements, ctx->scope_chain,
            expr->src_str, expr->src_len, &dispex);
    if(FAILED(hres))
        return hres;

    var_set_jsdisp(&v, dispex);
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT array_expression_eval(script_ctx_t *ctx, expression_t *_expr, jsexcept_t *ei, exprval_t *ret)
{
    binary_expression_t *expr = (binary_expression_t*)_expr;
    exprval_t exprval;
    VARIANT member, val;
    DISPID id;
    BSTR str;
    IDispatch *obj = NULL;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, expr->expression1, 0, ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, ei, &member);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    hres = expr_eval(ctx, expr->expression2, 0, ei, &exprval);
    if(SUCCEEDED(hres)) {
        hres = exprval_to_value(ctx, &exprval, ei, &val);
        exprval_release(&exprval);
    }

    if(SUCCEEDED(hres)) {
        hres = to_object(ctx, &member, &obj);
        if(FAILED(hres))
            VariantClear(&val);
    }
    VariantClear(&member);
    if(SUCCEEDED(hres)) {
        hres = to_string(ctx, &val, ei, &str);
        VariantClear(&val);
        if(SUCCEEDED(hres)) {
            hres = disp_get_id(ctx, obj, str, fdexNameEnsure, &id);
            SysFreeString(str);
        }

        if(SUCCEEDED(hres))
            exprval_set_idref(ret, obj, id);

        IDispatch_Release(obj);
    }

    return hres;
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT interp_array(exec_ctx_t *ctx)
{
    VARIANT v, *namev;
    IDispatch *obj;
    DISPID id;
    BSTR name;
    HRESULT hres;

    TRACE("\n");

    namev = stack_pop(ctx);

    hres = stack_pop_object(ctx, &obj);
    if(FAILED(hres)) {
        VariantClear(namev);
        return hres;
    }

    hres = to_string(ctx->parser->script, namev, &ctx->ei, &name);
    VariantClear(namev);
    if(FAILED(hres)) {
        IDispatch_Release(obj);
        return hres;
    }

    hres = disp_get_id(ctx->parser->script, obj, name, 0, &id);
    SysFreeString(name);
    if(SUCCEEDED(hres)) {
        hres = disp_propget(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
    }else if(hres == DISP_E_UNKNOWNNAME) {
        V_VT(&v) = VT_EMPTY;
        hres = S_OK;
    }
    IDispatch_Release(obj);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT member_expression_eval(script_ctx_t *ctx, expression_t *_expr, jsexcept_t *ei, exprval_t *ret)
{
    member_expression_t *expr = (member_expression_t*)_expr;
    IDispatch *obj = NULL;
    exprval_t exprval;
    VARIANT member;
    DISPID id;
    BSTR str;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx, expr->expression, 0, ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx, &exprval, ei, &member);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    hres = to_object(ctx, &member, &obj);
    VariantClear(&member);
    if(FAILED(hres))
        return hres;

    str = SysAllocString(expr->identifier);
    if(!str) {
        IDispatch_Release(obj);
        return E_OUTOFMEMORY;
    }

    hres = disp_get_id(ctx, obj, str, fdexNameEnsure, &id);
    SysFreeString(str);
    if(SUCCEEDED(hres))
        exprval_set_idref(ret, obj, id);

    IDispatch_Release(obj);
    return hres;
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT interp_member(exec_ctx_t *ctx)
{
    const BSTR arg = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    IDispatch *obj;
    VARIANT v;
    DISPID id;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_object(ctx, &obj);
    if(FAILED(hres))
        return hres;

    hres = disp_get_id(ctx->parser->script, obj, arg, 0, &id);
    if(SUCCEEDED(hres)) {
        V_VT(&v) = VT_EMPTY;
        hres = disp_propget(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
    }else if(hres == DISP_E_UNKNOWNNAME) {
        V_VT(&v) = VT_EMPTY;
        hres = S_OK;
    }
    IDispatch_Release(obj);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT interp_memberid(exec_ctx_t *ctx)
{
    const unsigned arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;
    VARIANT *objv, *namev;
    IDispatch *obj;
    BSTR name;
    DISPID id;
    HRESULT hres;

    TRACE("%x\n", arg);

    namev = stack_pop(ctx);
    objv = stack_pop(ctx);

    hres = to_object(ctx->parser->script, objv, &obj);
    VariantClear(objv);
    if(SUCCEEDED(hres)) {
        hres = to_string(ctx->parser->script, namev, &ctx->ei, &name);
        if(FAILED(hres))
            IDispatch_Release(obj);
    }
    VariantClear(namev);
    if(FAILED(hres))
        return hres;

    hres = disp_get_id(ctx->parser->script, obj, name, arg, &id);
    SysFreeString(name);
    if(FAILED(hres)) {
        IDispatch_Release(obj);
        if(hres == DISP_E_UNKNOWNNAME && !(arg & fdexNameEnsure)) {
            obj = NULL;
            id = JS_E_INVALID_PROPERTY;
        }else {
            return hres;
        }
    }

    return stack_push_objid(ctx, obj, id);
}

/* ECMA-262 3rd Edition    11.2.1 */
static HRESULT interp_refval(exec_ctx_t *ctx)
{
    IDispatch *disp;
    VARIANT v;
    DISPID id;
    HRESULT hres;

    TRACE("\n");

    disp = stack_topn_objid(ctx, 0, &id);
    if(!disp)
        return throw_reference_error(ctx->parser->script, &ctx->ei, JS_E_ILLEGAL_ASSIGN, NULL);

    hres = disp_propget(ctx->parser->script, disp, id, &v, &ctx->ei, NULL/*FIXME*/);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

static void jsstack_to_dp(exec_ctx_t *ctx, unsigned arg_cnt, DISPPARAMS *dp)
{
    VARIANT tmp;
    unsigned i;

    dp->cArgs = arg_cnt;
    dp->rgdispidNamedArgs = NULL;
    dp->cNamedArgs = 0;

    assert(ctx->top >= arg_cnt);

    for(i=1; i*2 <= arg_cnt; i++) {
        tmp = ctx->stack[ctx->top-i];
        ctx->stack[ctx->top-i] = ctx->stack[ctx->top-arg_cnt+i-1];
        ctx->stack[ctx->top-arg_cnt+i-1] = tmp;
    }

    dp->rgvarg = ctx->stack + ctx->top-arg_cnt;
}

/* ECMA-262 3rd Edition    11.2.2 */
static HRESULT interp_new(exec_ctx_t *ctx)
{
    const LONG arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;
    VARIANT *constr, v;
    DISPPARAMS dp;
    HRESULT hres;

    TRACE("%d\n", arg);

    constr = stack_topn(ctx, arg);

    /* NOTE: Should use to_object here */

    if(V_VT(constr) == VT_NULL)
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_OBJECT_EXPECTED, NULL);
    else if(V_VT(constr) != VT_DISPATCH)
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_INVALID_ACTION, NULL);
    else if(!V_DISPATCH(constr))
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_INVALID_PROPERTY, NULL);

    jsstack_to_dp(ctx, arg, &dp);
    hres = disp_call(ctx->parser->script, V_DISPATCH(constr), DISPID_VALUE,
            DISPATCH_CONSTRUCT, &dp, &v, &ctx->ei, NULL/*FIXME*/);
    if(FAILED(hres))
        return hres;

    stack_popn(ctx, arg+1);
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.2.3 */
static HRESULT interp_call(exec_ctx_t *ctx)
{
    const unsigned argn = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    const int do_ret = ctx->parser->code->instrs[ctx->ip].arg2.lng;
    VARIANT v, *objv;
    DISPPARAMS dp;
    HRESULT hres;

    TRACE("%d %d\n", argn, do_ret);

    objv = stack_topn(ctx, argn);
    if(V_VT(objv) != VT_DISPATCH)
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_INVALID_PROPERTY, NULL);

    jsstack_to_dp(ctx, argn, &dp);
    hres = disp_call(ctx->parser->script, V_DISPATCH(objv), DISPID_VALUE, DISPATCH_METHOD, &dp,
            do_ret ? &v : NULL, &ctx->ei, NULL/*FIXME*/);
    if(FAILED(hres))
        return hres;

    stack_popn(ctx, argn+1);
    return do_ret ? stack_push(ctx, &v) : S_OK;

}

/* ECMA-262 3rd Edition    11.2.3 */
static HRESULT interp_call_member(exec_ctx_t *ctx)
{
    const unsigned argn = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    const int do_ret = ctx->parser->code->instrs[ctx->ip].arg2.lng;
    IDispatch *obj;
    DISPPARAMS dp;
    VARIANT v;
    DISPID id;
    HRESULT hres;

    TRACE("%d %d\n", argn, do_ret);

    obj = stack_topn_objid(ctx, argn, &id);
    if(!obj)
        return throw_type_error(ctx->parser->script, &ctx->ei, id, NULL);

    jsstack_to_dp(ctx, argn, &dp);
    hres = disp_call(ctx->parser->script, obj, id, DISPATCH_METHOD, &dp, do_ret ? &v : NULL, &ctx->ei, NULL/*FIXME*/);
    if(FAILED(hres))
        return hres;

    stack_popn(ctx, argn+2);
    return do_ret ? stack_push(ctx, &v) : S_OK;

}

/* ECMA-262 3rd Edition    11.1.1 */
static HRESULT interp_this(exec_ctx_t *ctx)
{
    VARIANT v;

    TRACE("\n");

    V_VT(&v) = VT_DISPATCH;
    V_DISPATCH(&v) = ctx->this_obj;
    IDispatch_AddRef(ctx->this_obj);
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    10.1.4 */
static HRESULT identifier_expression_eval(script_ctx_t *ctx, expression_t *_expr, jsexcept_t *ei, exprval_t *ret)
{
    identifier_expression_t *expr = (identifier_expression_t*)_expr;
    BSTR identifier;
    HRESULT hres;

    TRACE("\n");

    identifier = SysAllocString(expr->identifier);
    if(!identifier)
        return E_OUTOFMEMORY;

    hres = identifier_eval(ctx, identifier, fdexNameEnsure, ei, ret);

    SysFreeString(identifier);
    return hres;
}

/* ECMA-262 3rd Edition    10.1.4 */
static HRESULT interp_ident(exec_ctx_t *ctx)
{
    const BSTR arg = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    exprval_t exprval;
    VARIANT v;
    HRESULT hres;

    TRACE("%s\n", debugstr_w(arg));

    hres = identifier_eval(ctx->parser->script, arg, 0, &ctx->ei, &exprval);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx->parser->script, &exprval, &ctx->ei, &v);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    10.1.4 */
static HRESULT interp_identid(exec_ctx_t *ctx)
{
    const BSTR arg = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    const unsigned flags = ctx->parser->code->instrs[ctx->ip].arg2.uint;
    exprval_t exprval;
    HRESULT hres;

    TRACE("%s %x\n", debugstr_w(arg), flags);

    hres = identifier_eval(ctx->parser->script, arg, flags, &ctx->ei, &exprval);
    if(FAILED(hres))
        return hres;

    if(exprval.type != EXPRVAL_IDREF) {
        WARN("invalid ref\n");
        exprval_release(&exprval);
        return stack_push_objid(ctx, NULL, JS_E_OBJECT_EXPECTED);
    }

    return stack_push_objid(ctx, exprval.u.idref.disp, exprval.u.idref.id);
}

/* ECMA-262 3rd Edition    7.8.1 */
static HRESULT interp_null(exec_ctx_t *ctx)
{
    VARIANT v;

    TRACE("\n");

    V_VT(&v) = VT_NULL;
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    7.8.2 */
static HRESULT interp_bool(exec_ctx_t *ctx)
{
    const LONG arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;

    TRACE("%s\n", arg ? "true" : "false");

    return stack_push_bool(ctx, arg);
}

/* ECMA-262 3rd Edition    7.8.3 */
static HRESULT interp_int(exec_ctx_t *ctx)
{
    const LONG arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;
    VARIANT v;

    TRACE("%d\n", arg);

    V_VT(&v) = VT_I4;
    V_I4(&v) = arg;
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    7.8.3 */
static HRESULT interp_double(exec_ctx_t *ctx)
{
    const double arg = *ctx->parser->code->instrs[ctx->ip].arg1.dbl;
    VARIANT v;

    TRACE("%lf\n", arg);

    V_VT(&v) = VT_R8;
    V_R8(&v) = arg;
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    7.8.4 */
static HRESULT interp_str(exec_ctx_t *ctx)
{
    const WCHAR *str = ctx->parser->code->instrs[ctx->ip].arg1.str;
    VARIANT v;

    TRACE("%s\n", debugstr_w(str));

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(str);
    if(!V_BSTR(&v))
        return E_OUTOFMEMORY;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    7.8 */
static HRESULT interp_regexp(exec_ctx_t *ctx)
{
    const WCHAR *source = ctx->parser->code->instrs[ctx->ip].arg1.str;
    const LONG flags = ctx->parser->code->instrs[ctx->ip].arg2.lng;
    jsdisp_t *regexp;
    VARIANT v;
    HRESULT hres;

    TRACE("%s %x\n", debugstr_w(source), flags);

    hres = create_regexp(ctx->parser->script, source, strlenW(source), flags, &regexp);
    if(FAILED(hres))
        return hres;

    var_set_jsdisp(&v, regexp);
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.1.4 */
static HRESULT interp_carray(exec_ctx_t *ctx)
{
    const unsigned arg = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    jsdisp_t *array;
    VARIANT *v, r;
    unsigned i;
    HRESULT hres;

    TRACE("%u\n", arg);

    hres = create_array(ctx->parser->script, arg, &array);
    if(FAILED(hres))
        return hres;

    i = arg;
    while(i--) {
        v = stack_pop(ctx);
        hres = jsdisp_propput_idx(array, i, v, &ctx->ei, NULL/*FIXME*/);
        VariantClear(v);
        if(FAILED(hres)) {
            jsdisp_release(array);
            return hres;
        }
    }

    var_set_jsdisp(&r, array);
    return stack_push(ctx, &r);
}

/* ECMA-262 3rd Edition    11.1.5 */
HRESULT interp_new_obj(exec_ctx_t *ctx)
{
    jsdisp_t *obj;
    VARIANT v;
    HRESULT hres;

    TRACE("\n");

    hres = create_object(ctx->parser->script, NULL, &obj);
    if(FAILED(hres))
        return hres;

    var_set_jsdisp(&v, obj);
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.1.5 */
HRESULT interp_obj_prop(exec_ctx_t *ctx)
{
    const BSTR name = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    jsdisp_t *obj;
    VARIANT *v;
    HRESULT hres;

    TRACE("%s\n", debugstr_w(name));

    v = stack_pop(ctx);

    assert(V_VT(stack_top(ctx)) == VT_DISPATCH);
    obj = as_jsdisp(V_DISPATCH(stack_top(ctx)));

    hres = jsdisp_propput_name(obj, name, v, &ctx->ei, NULL/*FIXME*/);
    VariantClear(v);
    return hres;
}

/* ECMA-262 3rd Edition    11.11 */
static HRESULT interp_jmp_nz(exec_ctx_t *ctx)
{
    const unsigned arg = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    VARIANT_BOOL b;
    HRESULT hres;

    TRACE("\n");

    hres = to_boolean(stack_top(ctx), &b);
    if(FAILED(hres))
        return hres;

    if(b) {
        ctx->ip = arg;
    }else {
        stack_popn(ctx, 1);
        ctx->ip++;
    }
    return S_OK;
}

/* ECMA-262 3rd Edition    11.11 */
static HRESULT interp_jmp_z(exec_ctx_t *ctx)
{
    const unsigned arg = ctx->parser->code->instrs[ctx->ip].arg1.uint;
    VARIANT_BOOL b;
    HRESULT hres;

    TRACE("\n");

    hres = to_boolean(stack_top(ctx), &b);
    if(FAILED(hres))
        return hres;

    if(b) {
        stack_popn(ctx, 1);
        ctx->ip++;
    }else {
        ctx->ip = arg;
    }
    return S_OK;
}

/* ECMA-262 3rd Edition    11.10 */
static HRESULT interp_or(exec_ctx_t *ctx)
{
    INT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_int(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_int(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l|r);
}

/* ECMA-262 3rd Edition    11.10 */
static HRESULT interp_xor(exec_ctx_t *ctx)
{
    INT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_int(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_int(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l^r);
}

/* ECMA-262 3rd Edition    11.10 */
static HRESULT interp_and(exec_ctx_t *ctx)
{
    INT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_int(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_int(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l&r);
}

/* ECMA-262 3rd Edition    11.8.6 */
static HRESULT interp_instanceof(exec_ctx_t *ctx)
{
    jsdisp_t *obj, *iter, *tmp = NULL;
    VARIANT prot, *v;
    BOOL ret = FALSE;
    HRESULT hres;

    static const WCHAR prototypeW[] = {'p','r','o','t','o','t', 'y', 'p','e',0};

    v = stack_pop(ctx);
    if(V_VT(v) != VT_DISPATCH || !V_DISPATCH(v)) {
        VariantClear(v);
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_FUNCTION_EXPECTED, NULL);
    }

    obj = iface_to_jsdisp((IUnknown*)V_DISPATCH(v));
    IDispatch_Release(V_DISPATCH(v));
    if(!obj) {
        FIXME("non-jsdisp objects not supported\n");
        return E_FAIL;
    }

    if(is_class(obj, JSCLASS_FUNCTION)) {
        hres = jsdisp_propget_name(obj, prototypeW, &prot, &ctx->ei, NULL/*FIXME*/);
    }else {
        hres = throw_type_error(ctx->parser->script, &ctx->ei, JS_E_FUNCTION_EXPECTED, NULL);
    }
    jsdisp_release(obj);
    if(FAILED(hres))
        return hres;

    v = stack_pop(ctx);

    if(V_VT(&prot) == VT_DISPATCH) {
        if(V_VT(v) == VT_DISPATCH)
            tmp = iface_to_jsdisp((IUnknown*)V_DISPATCH(v));
        for(iter = tmp; !ret && iter; iter = iter->prototype) {
            hres = disp_cmp(V_DISPATCH(&prot), to_disp(iter), &ret);
            if(FAILED(hres))
                break;
        }

        if(tmp)
            jsdisp_release(tmp);
    }else {
        FIXME("prototype is not an object\n");
        hres = E_FAIL;
    }

    VariantClear(&prot);
    VariantClear(v);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, ret);
}

/* ECMA-262 3rd Edition    11.8.7 */
static HRESULT interp_in(exec_ctx_t *ctx)
{
    VARIANT *obj, *v;
    DISPID id = 0;
    BOOL ret;
    BSTR str;
    HRESULT hres;

    TRACE("\n");

    obj = stack_pop(ctx);
    v = stack_pop(ctx);

    if(V_VT(obj) != VT_DISPATCH || !V_DISPATCH(obj)) {
        VariantClear(obj);
        VariantClear(v);
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_OBJECT_EXPECTED, NULL);
    }

    hres = to_string(ctx->parser->script, v, &ctx->ei, &str);
    VariantClear(v);
    if(FAILED(hres)) {
        IDispatch_Release(V_DISPATCH(obj));
        return hres;
    }

    hres = disp_get_id(ctx->parser->script, V_DISPATCH(obj), str, 0, &id);
    IDispatch_Release(V_DISPATCH(obj));
    SysFreeString(str);
    if(SUCCEEDED(hres))
        ret = TRUE;
    else if(hres == DISP_E_UNKNOWNNAME)
        ret = FALSE;
    else
        return hres;

    return stack_push_bool(ctx, ret);
}

/* ECMA-262 3rd Edition    11.6.1 */
static HRESULT add_eval(script_ctx_t *ctx, VARIANT *lval, VARIANT *rval, jsexcept_t *ei, VARIANT *retv)
{
    VARIANT r, l;
    HRESULT hres;

    hres = to_primitive(ctx, lval, ei, &l, NO_HINT);
    if(FAILED(hres))
        return hres;

    hres = to_primitive(ctx, rval, ei, &r, NO_HINT);
    if(FAILED(hres)) {
        VariantClear(&l);
        return hres;
    }

    if(V_VT(&l) == VT_BSTR || V_VT(&r) == VT_BSTR) {
        BSTR lstr = NULL, rstr = NULL;

        if(V_VT(&l) == VT_BSTR)
            lstr = V_BSTR(&l);
        else
            hres = to_string(ctx, &l, ei, &lstr);

        if(SUCCEEDED(hres)) {
            if(V_VT(&r) == VT_BSTR)
                rstr = V_BSTR(&r);
            else
                hres = to_string(ctx, &r, ei, &rstr);
        }

        if(SUCCEEDED(hres)) {
            int len1, len2;

            len1 = SysStringLen(lstr);
            len2 = SysStringLen(rstr);

            V_VT(retv) = VT_BSTR;
            V_BSTR(retv) = SysAllocStringLen(NULL, len1+len2);
            memcpy(V_BSTR(retv), lstr, len1*sizeof(WCHAR));
            memcpy(V_BSTR(retv)+len1, rstr, (len2+1)*sizeof(WCHAR));
        }

        if(V_VT(&l) != VT_BSTR)
            SysFreeString(lstr);
        if(V_VT(&r) != VT_BSTR)
            SysFreeString(rstr);
    }else {
        VARIANT nl, nr;

        hres = to_number(ctx, &l, ei, &nl);
        if(SUCCEEDED(hres)) {
            hres = to_number(ctx, &r, ei, &nr);
            if(SUCCEEDED(hres))
                num_set_val(retv, num_val(&nl) + num_val(&nr));
        }
    }

    VariantClear(&r);
    VariantClear(&l);
    return hres;
}

/* ECMA-262 3rd Edition    11.6.1 */
static HRESULT interp_add(exec_ctx_t *ctx)
{
    VARIANT *l, *r, ret;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s + %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = add_eval(ctx->parser->script, l, r, &ctx->ei, &ret);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &ret);
}

/* ECMA-262 3rd Edition    11.6.2 */
static HRESULT interp_sub(exec_ctx_t *ctx)
{
    VARIANT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_number(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_number(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_number(ctx, num_val(&l)-num_val(&r));
}

/* ECMA-262 3rd Edition    11.5.1 */
static HRESULT interp_mul(exec_ctx_t *ctx)
{
    VARIANT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_number(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_number(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_number(ctx, num_val(&l)*num_val(&r));
}

/* ECMA-262 3rd Edition    11.5.2 */
static HRESULT interp_div(exec_ctx_t *ctx)
{
    VARIANT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_number(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_number(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_number(ctx, num_val(&l)/num_val(&r));
}

/* ECMA-262 3rd Edition    11.5.3 */
static HRESULT interp_mod(exec_ctx_t *ctx)
{
    VARIANT l, r;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_number(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_number(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_number(ctx, fmod(num_val(&l), num_val(&r)));
}

/* ECMA-262 3rd Edition    11.4.2 */
static HRESULT interp_delete(exec_ctx_t *ctx)
{
    VARIANT *obj_var, *name_var;
    IDispatchEx *dispex;
    IDispatch *obj;
    BSTR name;
    BOOL ret;
    HRESULT hres;

    TRACE("\n");

    name_var = stack_pop(ctx);
    obj_var = stack_pop(ctx);

    hres = to_object(ctx->parser->script, obj_var, &obj);
    VariantClear(obj_var);
    if(FAILED(hres)) {
        VariantClear(name_var);
        return hres;
    }

    hres = to_string(ctx->parser->script, name_var, &ctx->ei, &name);
    VariantClear(name_var);
    if(FAILED(hres)) {
        IDispatch_Release(obj);
        return hres;
    }

    hres = IDispatch_QueryInterface(obj, &IID_IDispatchEx, (void**)&dispex);
    if(SUCCEEDED(hres)) {
        hres = IDispatchEx_DeleteMemberByName(dispex, name, make_grfdex(ctx->parser->script, fdexNameCaseSensitive));
        ret = TRUE;
        IDispatchEx_Release(dispex);
    }else {
        hres = S_OK;
        ret = FALSE;
    }

    IDispatch_Release(obj);
    SysFreeString(name);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, ret);
}

/* ECMA-262 3rd Edition    11.4.2 */
static HRESULT interp_delete_ident(exec_ctx_t *ctx)
{
    const BSTR arg = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    IDispatchEx *dispex;
    exprval_t exprval;
    BOOL ret = FALSE;
    HRESULT hres;

    TRACE("%s\n", debugstr_w(arg));

    hres = identifier_eval(ctx->parser->script, arg, 0, &ctx->ei, &exprval);
    if(FAILED(hres))
        return hres;

    if(exprval.type != EXPRVAL_IDREF) {
        FIXME("Unsupported exprval\n");
        exprval_release(&exprval);
        return E_NOTIMPL;
    }

    hres = IDispatch_QueryInterface(exprval.u.idref.disp, &IID_IDispatchEx, (void**)&dispex);
    IDispatch_Release(exprval.u.idref.disp);
    if(SUCCEEDED(hres)) {
        hres = IDispatchEx_DeleteMemberByDispID(dispex, exprval.u.idref.id);
        IDispatchEx_Release(dispex);
        if(FAILED(hres))
            return hres;

        ret = TRUE;
    }

    return stack_push_bool(ctx, ret);
}

/* ECMA-262 3rd Edition    11.4.2 */
static HRESULT interp_void(exec_ctx_t *ctx)
{
    VARIANT v;

    TRACE("\n");

    stack_popn(ctx, 1);

    V_VT(&v) = VT_EMPTY;
    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.4.3 */
static HRESULT typeof_string(VARIANT *v, const WCHAR **ret)
{
    switch(V_VT(v)) {
    case VT_EMPTY:
        *ret = undefinedW;
        break;
    case VT_NULL:
        *ret = objectW;
        break;
    case VT_BOOL:
        *ret = booleanW;
        break;
    case VT_I4:
    case VT_R8:
        *ret = numberW;
        break;
    case VT_BSTR:
        *ret = stringW;
        break;
    case VT_DISPATCH: {
        jsdisp_t *dispex;

        if(V_DISPATCH(v) && (dispex = iface_to_jsdisp((IUnknown*)V_DISPATCH(v)))) {
            *ret = is_class(dispex, JSCLASS_FUNCTION) ? functionW : objectW;
            jsdisp_release(dispex);
        }else {
            *ret = objectW;
        }
        break;
    }
    default:
        FIXME("unhandled vt %d\n", V_VT(v));
        return E_NOTIMPL;
    }

    return S_OK;
}

/* ECMA-262 3rd Edition    11.4.3 */
static HRESULT interp_typeofid(exec_ctx_t *ctx)
{
    const WCHAR *ret;
    IDispatch *obj;
    VARIANT v;
    DISPID id;
    HRESULT hres;

    static const WCHAR undefinedW[] = {'u','n','d','e','f','i','n','e','d',0};

    TRACE("\n");

    obj = stack_pop_objid(ctx, &id);
    if(!obj)
        return stack_push_string(ctx, undefinedW);

    V_VT(&v) = VT_EMPTY;
    hres = disp_propget(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
    IDispatch_Release(obj);
    if(FAILED(hres))
        return stack_push_string(ctx, unknownW);

    hres = typeof_string(&v, &ret);
    VariantClear(&v);
    if(FAILED(hres))
        return hres;

    return stack_push_string(ctx, ret);
}

/* ECMA-262 3rd Edition    11.4.3 */
static HRESULT interp_typeofident(exec_ctx_t *ctx)
{
    const BSTR arg = ctx->parser->code->instrs[ctx->ip].arg1.bstr;
    exprval_t exprval;
    const WCHAR *ret;
    VARIANT v;
    HRESULT hres;

    TRACE("%s\n", debugstr_w(arg));

    hres = identifier_eval(ctx->parser->script, arg, 0, &ctx->ei, &exprval);
    if(FAILED(hres))
        return hres;

    if(exprval.type == EXPRVAL_INVALID) {
        hres = stack_push_string(ctx, undefinedW);
        exprval_release(&exprval);
        return hres;
    }

    hres = exprval_to_value(ctx->parser->script, &exprval, &ctx->ei, &v);
    exprval_release(&exprval);
    if(FAILED(hres))
        return hres;

    hres = typeof_string(&v, &ret);
    VariantClear(&v);
    if(FAILED(hres))
        return hres;

    return stack_push_string(ctx, ret);
}

/* ECMA-262 3rd Edition    11.4.3 */
static HRESULT interp_typeof(exec_ctx_t *ctx)
{
    const WCHAR *ret;
    VARIANT *v;
    HRESULT hres;

    TRACE("\n");

    v = stack_pop(ctx);
    hres = typeof_string(v, &ret);
    VariantClear(v);
    if(FAILED(hres))
        return hres;

    return stack_push_string(ctx, ret);
}

/* ECMA-262 3rd Edition    11.4.7 */
static HRESULT interp_minus(exec_ctx_t *ctx)
{
    VARIANT n;
    HRESULT hres;

    TRACE("\n");

    hres = stack_pop_number(ctx, &n);
    if(FAILED(hres))
        return hres;

    return stack_push_number(ctx, -num_val(&n));
}

/* ECMA-262 3rd Edition    11.4.6 */
static HRESULT interp_tonum(exec_ctx_t *ctx)
{
    VARIANT *v, num;
    HRESULT hres;

    TRACE("\n");

    v = stack_pop(ctx);
    hres = to_number(ctx->parser->script, v, &ctx->ei, &num);
    VariantClear(v);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &num);
}

/* ECMA-262 3rd Edition    11.3.1 */
static HRESULT interp_postinc(exec_ctx_t *ctx)
{
    const int arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;
    IDispatch *obj;
    DISPID id;
    VARIANT v;
    HRESULT hres;

    TRACE("%d\n", arg);

    obj = stack_pop_objid(ctx, &id);
    if(!obj)
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_OBJECT_EXPECTED, NULL);

    hres = disp_propget(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
    if(SUCCEEDED(hres)) {
        VARIANT n, inc;

        hres = to_number(ctx->parser->script, &v, &ctx->ei, &n);
        if(SUCCEEDED(hres)) {
            num_set_val(&inc, num_val(&n)+(double)arg);
            hres = disp_propput(ctx->parser->script, obj, id, &inc, &ctx->ei, NULL/*FIXME*/);
        }
        if(FAILED(hres))
            VariantClear(&v);
    }
    IDispatch_Release(obj);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.4.4, 11.4.5 */
static HRESULT interp_preinc(exec_ctx_t *ctx)
{
    const int arg = ctx->parser->code->instrs[ctx->ip].arg1.lng;
    IDispatch *obj;
    DISPID id;
    VARIANT v;
    HRESULT hres;

    TRACE("%d\n", arg);

    obj = stack_pop_objid(ctx, &id);
    if(!obj)
        return throw_type_error(ctx->parser->script, &ctx->ei, JS_E_OBJECT_EXPECTED, NULL);

    hres = disp_propget(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
    if(SUCCEEDED(hres)) {
        VARIANT n;

        hres = to_number(ctx->parser->script, &v, &ctx->ei, &n);
        VariantClear(&v);
        if(SUCCEEDED(hres)) {
            num_set_val(&v, num_val(&n)+(double)arg);
            hres = disp_propput(ctx->parser->script, obj, id, &v, &ctx->ei, NULL/*FIXME*/);
        }
    }
    IDispatch_Release(obj);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

/* ECMA-262 3rd Edition    11.9.3 */
static HRESULT equal_values(script_ctx_t *ctx, VARIANT *lval, VARIANT *rval, jsexcept_t *ei, BOOL *ret)
{
    if(V_VT(lval) == V_VT(rval) || (is_num_vt(V_VT(lval)) && is_num_vt(V_VT(rval))))
       return equal2_values(lval, rval, ret);

    /* FIXME: NULL disps should be handled in more general way */
    if(V_VT(lval) == VT_DISPATCH && !V_DISPATCH(lval)) {
        VARIANT v;
        V_VT(&v) = VT_NULL;
        return equal_values(ctx, &v, rval, ei, ret);
    }

    if(V_VT(rval) == VT_DISPATCH && !V_DISPATCH(rval)) {
        VARIANT v;
        V_VT(&v) = VT_NULL;
        return equal_values(ctx, lval, &v, ei, ret);
    }

    if((V_VT(lval) == VT_NULL && V_VT(rval) == VT_EMPTY) ||
       (V_VT(lval) == VT_EMPTY && V_VT(rval) == VT_NULL)) {
        *ret = TRUE;
        return S_OK;
    }

    if(V_VT(lval) == VT_BSTR && is_num_vt(V_VT(rval))) {
        VARIANT v;
        HRESULT hres;

        hres = to_number(ctx, lval, ei, &v);
        if(FAILED(hres))
            return hres;

        return equal_values(ctx, &v, rval, ei, ret);
    }

    if(V_VT(rval) == VT_BSTR && is_num_vt(V_VT(lval))) {
        VARIANT v;
        HRESULT hres;

        hres = to_number(ctx, rval, ei, &v);
        if(FAILED(hres))
            return hres;

        return equal_values(ctx, lval, &v, ei, ret);
    }

    if(V_VT(rval) == VT_BOOL) {
        VARIANT v;

        V_VT(&v) = VT_I4;
        V_I4(&v) = V_BOOL(rval) ? 1 : 0;
        return equal_values(ctx, lval, &v, ei, ret);
    }

    if(V_VT(lval) == VT_BOOL) {
        VARIANT v;

        V_VT(&v) = VT_I4;
        V_I4(&v) = V_BOOL(lval) ? 1 : 0;
        return equal_values(ctx, &v, rval, ei, ret);
    }


    if(V_VT(rval) == VT_DISPATCH && (V_VT(lval) == VT_BSTR || is_num_vt(V_VT(lval)))) {
        VARIANT v;
        HRESULT hres;

        hres = to_primitive(ctx, rval, ei, &v, NO_HINT);
        if(FAILED(hres))
            return hres;

        hres = equal_values(ctx, lval, &v, ei, ret);

        VariantClear(&v);
        return hres;
    }


    if(V_VT(lval) == VT_DISPATCH && (V_VT(rval) == VT_BSTR || is_num_vt(V_VT(rval)))) {
        VARIANT v;
        HRESULT hres;

        hres = to_primitive(ctx, lval, ei, &v, NO_HINT);
        if(FAILED(hres))
            return hres;

        hres = equal_values(ctx, &v, rval, ei, ret);

        VariantClear(&v);
        return hres;
    }


    *ret = FALSE;
    return S_OK;
}

/* ECMA-262 3rd Edition    11.9.1 */
static HRESULT interp_eq(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s == %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = equal_values(ctx->parser->script, l, r, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.9.2 */
static HRESULT interp_neq(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s != %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = equal_values(ctx->parser->script, l, r, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, !b);
}

/* ECMA-262 3rd Edition    11.9.4 */
static HRESULT interp_eq2(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    TRACE("\n");

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    hres = equal2_values(r, l, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.9.5 */
static HRESULT interp_neq2(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    TRACE("\n");

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    hres = equal2_values(r, l, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, !b);
}

/* ECMA-262 3rd Edition    11.8.5 */
static HRESULT less_eval(script_ctx_t *ctx, VARIANT *lval, VARIANT *rval, BOOL greater, jsexcept_t *ei, BOOL *ret)
{
    VARIANT l, r, ln, rn;
    HRESULT hres;

    hres = to_primitive(ctx, lval, ei, &l, NO_HINT);
    if(FAILED(hres))
        return hres;

    hres = to_primitive(ctx, rval, ei, &r, NO_HINT);
    if(FAILED(hres)) {
        VariantClear(&l);
        return hres;
    }

    if(V_VT(&l) == VT_BSTR && V_VT(&r) == VT_BSTR) {
        *ret = (strcmpW(V_BSTR(&l), V_BSTR(&r)) < 0) ^ greater;
        SysFreeString(V_BSTR(&l));
        SysFreeString(V_BSTR(&r));
        return S_OK;
    }

    hres = to_number(ctx, &l, ei, &ln);
    VariantClear(&l);
    if(SUCCEEDED(hres))
        hres = to_number(ctx, &r, ei, &rn);
    VariantClear(&r);
    if(FAILED(hres))
        return hres;

    if(V_VT(&ln) == VT_I4 && V_VT(&rn) == VT_I4) {
        *ret = (V_I4(&ln) < V_I4(&rn)) ^ greater;
    }else  {
        DOUBLE ld = num_val(&ln);
        DOUBLE rd = num_val(&rn);

        *ret = !isnan(ld) && !isnan(rd) && ((ld < rd) ^ greater);
    }

    return S_OK;
}

/* ECMA-262 3rd Edition    11.8.1 */
static HRESULT interp_lt(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s < %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = less_eval(ctx->parser->script, l, r, FALSE, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.8.1 */
static HRESULT interp_lteq(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s <= %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = less_eval(ctx->parser->script, r, l, TRUE, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.8.2 */
static HRESULT interp_gt(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s > %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = less_eval(ctx->parser->script, r, l, FALSE, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.8.4 */
static HRESULT interp_gteq(exec_ctx_t *ctx)
{
    VARIANT *l, *r;
    BOOL b;
    HRESULT hres;

    r = stack_pop(ctx);
    l = stack_pop(ctx);

    TRACE("%s >= %s\n", debugstr_variant(l), debugstr_variant(r));

    hres = less_eval(ctx->parser->script, l, r, TRUE, &ctx->ei, &b);
    VariantClear(l);
    VariantClear(r);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, b);
}

/* ECMA-262 3rd Edition    11.4.8 */
static HRESULT interp_bneg(exec_ctx_t *ctx)
{
    VARIANT *v, r;
    INT i;
    HRESULT hres;

    TRACE("\n");

    v = stack_pop(ctx);
    hres = to_int32(ctx->parser->script, v, &ctx->ei, &i);
    VariantClear(v);
    if(FAILED(hres))
        return hres;

    V_VT(&r) = VT_I4;
    V_I4(&r) = ~i;
    return stack_push(ctx, &r);
}

/* ECMA-262 3rd Edition    11.4.9 */
static HRESULT interp_neg(exec_ctx_t *ctx)
{
    VARIANT *v;
    VARIANT_BOOL b;
    HRESULT hres;

    TRACE("\n");

    v = stack_pop(ctx);
    hres = to_boolean(v, &b);
    VariantClear(v);
    if(FAILED(hres))
        return hres;

    return stack_push_bool(ctx, !b);
}

/* ECMA-262 3rd Edition    11.7.1 */
static HRESULT interp_lshift(exec_ctx_t *ctx)
{
    DWORD r;
    INT l;
    HRESULT hres;

    hres = stack_pop_uint(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_int(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l << (r&0x1f));
}

/* ECMA-262 3rd Edition    11.7.2 */
static HRESULT interp_rshift(exec_ctx_t *ctx)
{
    DWORD r;
    INT l;
    HRESULT hres;

    hres = stack_pop_uint(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_int(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l >> (r&0x1f));
}

/* ECMA-262 3rd Edition    11.7.3 */
static HRESULT interp_rshift2(exec_ctx_t *ctx)
{
    DWORD r, l;
    HRESULT hres;

    hres = stack_pop_uint(ctx, &r);
    if(FAILED(hres))
        return hres;

    hres = stack_pop_uint(ctx, &l);
    if(FAILED(hres))
        return hres;

    return stack_push_int(ctx, l >> (r&0x1f));
}

/* ECMA-262 3rd Edition    11.13.1 */
static HRESULT interp_assign(exec_ctx_t *ctx)
{
    IDispatch *disp;
    DISPID id;
    VARIANT *v;
    HRESULT hres;

    TRACE("\n");

    v = stack_pop(ctx);
    disp = stack_pop_objid(ctx, &id);

    if(!disp)
        return throw_reference_error(ctx->parser->script, &ctx->ei, JS_E_ILLEGAL_ASSIGN, NULL);

    hres = disp_propput(ctx->parser->script, disp, id, v, &ctx->ei, NULL/*FIXME*/);
    IDispatch_Release(disp);
    if(FAILED(hres)) {
        VariantClear(v);
        return hres;
    }

    return stack_push(ctx, v);
}

static HRESULT interp_undefined(exec_ctx_t *ctx)
{
    VARIANT v;

    TRACE("\n");

    V_VT(&v) = VT_EMPTY;
    return stack_push(ctx, &v);
}

static HRESULT interp_jmp(exec_ctx_t *ctx)
{
    const unsigned arg = ctx->parser->code->instrs[ctx->ip].arg1.uint;

    TRACE("\n");

    ctx->ip = arg;
    return S_OK;
}

static HRESULT interp_pop(exec_ctx_t *ctx)
{
    TRACE("\n");

    stack_popn(ctx, 1);
    return S_OK;
}

static HRESULT interp_ret(exec_ctx_t *ctx)
{
    TRACE("\n");

    ctx->ip = -1;
    return S_OK;
}

static HRESULT interp_tree(exec_ctx_t *ctx)
{
    instr_t *instr = ctx->parser->code->instrs+ctx->ip;
    exprval_t val;
    VARIANT v;
    HRESULT hres;

    TRACE("\n");

    hres = expr_eval(ctx->parser->script, instr->arg1.expr, 0, &ctx->ei, &val);
    if(FAILED(hres))
        return hres;

    hres = exprval_to_value(ctx->parser->script, &val, &ctx->ei, &v);
    exprval_release(&val);
    if(FAILED(hres))
        return hres;

    return stack_push(ctx, &v);
}

typedef HRESULT (*op_func_t)(exec_ctx_t*);

static const op_func_t op_funcs[] = {
#define X(x,a,b,c) interp_##x,
OP_LIST
#undef X
};

static const unsigned op_move[] = {
#define X(a,x,b,c) x,
OP_LIST
#undef X
};

static HRESULT expr_eval(script_ctx_t *ctx, expression_t *expr, DWORD flags, jsexcept_t *ei, exprval_t *ret)
{
    exec_ctx_t *exec_ctx = ctx->exec_ctx;
    unsigned prev_ip, prev_top;
    jsop_t op;
    HRESULT hres = S_OK;

    TRACE("\n");

    if(expr->instr_off == -1) {
        hres = compile_subscript(ctx->exec_ctx->parser, expr, !(flags & EXPR_NOVAL), &expr->instr_off);
        if(FAILED(hres))
            return hres;
    }

    prev_top = exec_ctx->top;
    prev_ip = exec_ctx->ip;
    exec_ctx->ip = expr->instr_off;

    while(exec_ctx->ip != -1) {
        op = exec_ctx->parser->code->instrs[exec_ctx->ip].op;
        hres = op_funcs[op](exec_ctx);
        if(FAILED(hres))
            break;
        exec_ctx->ip += op_move[op];
    }

    exec_ctx->ip = prev_ip;

    if(FAILED(hres)) {
        stack_popn(exec_ctx, exec_ctx->top-prev_top);
        *ei = exec_ctx->ei;
        memset(&exec_ctx->ei, 0, sizeof(exec_ctx->ei));
        return hres;
    }

    assert(exec_ctx->top == prev_top+1 || ((flags&EXPR_NOVAL) && exec_ctx->top == prev_top));

    ret->type = EXPRVAL_VARIANT;
    if(exec_ctx->top == prev_top)
        V_VT(&ret->u.var) = VT_EMPTY;
    else
        ret->u.var = *stack_pop(exec_ctx);
    return S_OK;
}
