#include "node.h"
#include "codegen.h"
#include "parser.hpp"

using namespace std;

/* Compile AST into a module*/
void CodeGenContext::generateCode(NBlock& root) {
	cout << "Generating code...\n";

	/* Create top level interpreter function to call as entry*/
	vector<const Type*> argTypes;
	FunctionType *ftype = FunctionType::get(Type::getVoidTy(getGlobalContext()), argTypes, false);
	mainFunction = Function::Create(ftype, GlobalValue::InternalLinkage, "main", module);
	BasicBlock *bblock = BasicBlock::Create(getGlobalContext(), "entry", mainFunction, 0);

	/* Push a new variable/block context */
	pushBlock(bblock);
	root.codeGen(*this); /* Emit bytecode for toplevel block*/
	ReturnInst::Create(getGlobalContext(), bblock);
	popBlock();

	cout << "Code is generated.\n";
	/*Print the bytecode*/
	PassManager pm;
	pm.add(createPrintModulePass(&outs()));
	pm.run(*module);
}

/* Executes program main function*/
GenericValue CodeGenContext::runCode() {
	cout << "Running code...\n";
	InitializeNativeTarget();
	ExecutionEngine *ee = EngineBuilder(module).create();
	vector<GenericValue> noargs;
	GenericValue v = ee->runFunction(mainFunction, noargs);
  	ee->freeMachineCodeForFunction(mainFunction);
  	delete ee;
  	llvm_shutdown();	
	cout << "Code was run.\n";
	return v;
}

/* Returns a LLVM type based on the identifier */
static const Type *typeOf(const NIdentifier type) {
	if (type.name.compare("int") == 0) {
		return Type::getInt64Ty(getGlobalContext());
	} else if (type.name.compare("double") == 0) {
		return Type::getDoubleTy(getGlobalContext());
	}
	return Type::getVoidTy(getGlobalContext());
}

/* Code Generation */
Value* NInteger::codeGen(CodeGenContext& context) {
	cout << "Creating Integer: " << value << endl;
	return ConstantInt::get(Type::getInt64Ty(getGlobalContext()), value, true);
}

Value* NDouble::codeGen(CodeGenContext& context) {
	cout << "Creating Double: " << value << endl;
	return ConstantFP::get(Type::getDoubleTy(getGlobalContext()), value);
}

Value* NIdentifier::codeGen(CodeGenContext& context) {
	cout << "Creating identifier reference: " << name << endl;
	if (context.locals().find(name) == context.locals().end()) {
		cerr << "undeclared variable " << name << endl;
		return NULL;
	}
	return new LoadInst(context.locals()[name], "", false, context.currentBlock());
}

Value* NMethodCall::codeGen(CodeGenContext& context) {
	Function *function = context.module->getFunction(id.name.c_str());
	if (function == NULL) {
		cerr << "no such function " << id.name << endl;
		// return status error ??
	}
	/* Execute expressions in arguments */
	std::vector<Value*> args;
	ExpressionList::const_iterator it;
	for (it = arguments.begin(); it != arguments.end(); it++) {
		args.push_back((**it).codeGen(context));
	}
	/* Effectively call the method*/
	CallInst *call = CallInst::Create(function, args.begin(), args.end(), "", context.currentBlock());

	cout << "Creating method call: " << id.name << endl;
	return call;
	
}

Value* NBinaryOperator::codeGen(CodeGenContext& context) {
	cout << "Creating binary operation " << op << endl;
	Instruction::BinaryOps instr;
	switch (op) {
		case TPLUS:
			instr = Instruction::Add;
			goto math;
		case TMINUS:
			instr = Instruction::Sub;
			goto math;
		case TMUL:
			instr = Instruction::Mul;
			goto math;
		case TDIV:
			instr = Instruction::SDiv; 
			goto math;
		/* TODO comparison*/
	}
	return NULL;
math:
	return BinaryOperator::Create(instr, leftSide.codeGen(context), rightSide.codeGen(context), "",
			context.currentBlock());
}

Value* NBlock::codeGen(CodeGenContext& context) {
	StatementList::const_iterator it;
	Value *last = NULL;
	for (it = statements.begin(); it != statements.end(); it++) {
		cout << "Generating code for " << typeid(**it).name() << endl;
		last = (**it).codeGen(context);
	}
	cout << "Creating block" << endl;
	return last;
}

Value* NAssignment::codeGen(CodeGenContext& context) {
	cout << "Creating assignment for " << leftSide.name << endl;
	if (context.locals().find(leftSide.name) == context.locals().end()) {
		cerr << "undeclared variable " << leftSide.name << endl;
		return NULL;
	}
	return new StoreInst(rightSide.codeGen(context), context.locals()[leftSide.name], 
		false, context.currentBlock());
}

Value* NExpressionStatement::codeGen(CodeGenContext& context) {
	cout << "Generating code for " << typeid(expression).name() << endl;
	return expression.codeGen(context);
}

Value* NVariableDeclaration::codeGen(CodeGenContext& context) {
	cout << "Creating variable declaration " << type.name << " " << id.name << endl;
	AllocaInst *alloc = new AllocaInst(typeOf(type), id.name.c_str(), context.currentBlock());
	context.locals()[id.name] = alloc;
	if (assignmentExpr != NULL) {
		NAssignment assn(id, *assignmentExpr);
		assn.codeGen(context);
	}
	return alloc;
}

Value* NFunctionDeclaration::codeGen(CodeGenContext& context) {
	std::vector<const Type*> argTypes;
	VariableList::const_iterator it;
	for (it = arguments.begin(); it != arguments.end(); it++) {
		argTypes.push_back(typeOf((**it).type));
	}
	FunctionType *ftype = FunctionType::get(typeOf(type), argTypes, false);
	Function *function = Function::Create(ftype, GlobalValue::InternalLinkage, id.name.c_str(), context.module);
	BasicBlock *bblock = BasicBlock::Create(getGlobalContext(), "entry", function, 0);

	context.pushBlock(bblock);

	for (it = arguments.begin(); it != arguments.end(); it++) {
		(**it).codeGen(context);
	}

	block.codeGen(context);
	ReturnInst::Create(getGlobalContext(), bblock);

	context.popBlock();
	cout << "Creating function: " << id.name << endl;
	return function;
}
