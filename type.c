#include "lib.h"
#include "sparse/expression.h"
#include "sparse/symbol.h"
#include <assert.h>
#include <string.h>

static type_t emit_type_begin(struct symbol *sym)
{
	struct symbol *base_type = sym->ctype.base_type;

	if (sym == &void_ctype)
		return LLVMVoidType();

	// int_type is the base type of all [INTEGER]_ctype.
	if (base_type == &int_type)
		return LLVMIntType(sym->bit_size);

	if (sym->type == SYM_ENUM)
		return emit_type(base_type);

	if (sym->type == SYM_BITFIELD)
		return LLVMIntType(sym->bit_size);

	if (sym->type == SYM_PTR) {
		type_t elem_type = emit_type(base_type);

		// Fix up void * to i8 *.
		if (LLVMGetTypeKind(elem_type) == LLVMVoidTypeKind)
			elem_type = LLVMInt8Type();
		return LLVMPointerType(elem_type, sym->ctype.as);
	}

	if (sym->type == SYM_ARRAY) {
		type_t elem_type = emit_type(base_type);
		unsigned n = get_expression_value(sym->array_size);

		return LLVMArrayType(elem_type, n);
	}

	if (sym->type == SYM_STRUCT) {
		const char *prefix = "struct.";
		const char *name = sym->ident ? show_ident(sym->ident) : "anno";
		char buf[strlen(name) + strlen(prefix) + 1];

		strcpy(buf, prefix);
		strcat(buf, name);
		return LLVMStructCreateNamed(LLVMGetGlobalContext(), buf);
	}

	if (sym->type == SYM_UNION) {
		const char *prefix = "union.";
		const char *name = sym->ident ? show_ident(sym->ident) : "anno";
		char buf[strlen(name) + strlen(prefix) + 1];
		type_t type;

		strcpy(buf, prefix);
		strcat(buf, name);
		type = LLVMStructCreateNamed(LLVMGetGlobalContext(), buf);
		// Fill in union body if it is defined.
		if (sym->bit_size) {
			type_t elem_type = LLVMIntType(sym->bit_size);

			LLVMStructSetBody(type, &elem_type, 1, 0);
		}
		return type;
	}

	if (sym->type == SYM_FN) {
		struct symbol *arg;
		int n = symbol_list_size(sym->arguments), i;
		type_t ret_type, arg_types[n];

		// Return type default to int.
		if (base_type == &incomplete_ctype)
			ret_type = emit_type(&int_ctype);
		else
			ret_type = emit_type(base_type);
		i = 0;
		FOR_EACH_PTR(sym->arguments, arg) {
			arg_types[i++] = emit_type(arg->ctype.base_type);
		} END_FOR_EACH_PTR(arg);
		return LLVMFunctionType(ret_type, arg_types, n, sym->variadic);
	}

	if (sym == &float_ctype)
		return LLVMFloatType();

	if (sym == &double_ctype)
		return LLVMDoubleType();

	if (sym == &ldouble_ctype) {
		switch (sym->bit_size) {
		default: assert(0 && "Unknown long double size!");
		case 64: return LLVMDoubleType();
		case 80: return LLVMX86FP80Type();
		case 128: return LLVMFP128Type();
		}
	}

	show_symbol(sym);
	assert(0 && "Unknown type!");
}

static void emit_type_end(struct symbol *sym, type_t type)
{
	// Fill in struct body.
	if (sym->type == SYM_STRUCT) {
		int n = symbol_list_size(sym->symbol_list), i;
		type_t elem_types[n];
		struct symbol *member;

		assert(LLVMGetTypeKind(type) == LLVMStructTypeKind);
		if (n == 0)
			return;
		i = 0;
		FOR_EACH_PTR(sym->symbol_list, member) {
			elem_types[i++] = emit_type(member);
		} END_FOR_EACH_PTR(member);
		LLVMStructSetBody(type, elem_types, n, 0);
	}
}

static type_t emit_raw_type(struct symbol *sym)
{
	type_t type;

	if (sym->type == SYM_NODE)
		sym = sym->ctype.base_type;
	// ->aux points to llvm::Type.
	if (sym->aux)
		return sym->aux;
	type = emit_type_begin(sym);
	sym->aux = type;
	emit_type_end(sym, type);
	return type;
}

type_t emit_type(struct symbol *sym)
{
	type_t type = emit_raw_type(sym);

	// Fix array type arr[] = {...}.
	if (is_array_type(type) && LLVMGetArrayLength(type) == 0 && sym->initializer) {
		struct expression *expr = sym->initializer;
		int n = 0;

		if (expr->ctype->array_size)
			n = get_expression_value(expr->ctype->array_size);
		if (!n && expr->type == EXPR_INITIALIZER)
			n = expression_list_size(expr->expr_list);
		if (n)
			type = LLVMArrayType(LLVMGetElementType(type), n);
	}

	return type;
}
