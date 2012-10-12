#include "Rehydrate.h"
#include "FuncContents.h"
#include "ExprContents.h"
#include "Util.h"

#include <map>
#include <vector>
#include <set>
using std::map;
using std::vector;
using std::pair;
using std::set;

#include <iostream>
using std::cerr;
using std::endl;

namespace Halide {

    ML_FUNC1(deserializeEnv)
    ML_FUNC1(getEnvDefinitions)
    ML_FUNC1(typeOfExpr)
    ML_FUNC1(varsInExpr)
    ML_FUNC1(callsInExpr)

    ML_FUNC1(callIsFunc)
    ML_FUNC1(callIsExtern)
    ML_FUNC1(callIsImage)

    ML_FUNC1(listHead)
    ML_FUNC1(listTail)
    ML_FUNC1(listEmpty)

    // Mirror helpers for unpacking `definitions` from the deserialized environment.
    // Yes, this is ugly at this point. Better ideas (short of adding dozens of
    // accessor callback helpers to halide_cpp.ml) are welcome.
    struct Definition {
        Definition() {}

        Definition(MLVal d) : name(string(d[0])), ret_t(d[2]), body(d[3]) {
            for (MLVal list = d[1]; !listEmpty(list); list = listTail(list)) {
                MLVal arg = listHead(list);
                args.push_back(make_pair(string(arg[1]), arg[0]));
            }
        }

        struct Body {
            Body() {}

            Body(MLVal b) {
                if (functionIsPure(b)) {
                    ty = Pure;
                    rhs = getPureBody(b);
                } else {
                    assert(functionIsReduce(b));
                    ty = Reduce;
                    MLVal red = getReduceBody(b);
                    rhs = red[0];
                    updateLoc = red[1];
                    updateFunc = string(red[2]);
                    dom = red[3];
                }
            }

            enum {
                Pure,
                Reduce
            } ty;

            MLVal rhs; // pure or initializer expr

            // if reduce:
            MLVal updateLoc;
            string updateFunc;
            MLVal dom;
        };

        string name;
        vector<pair<string, Type> > args;
        Type ret_t;
        Body body;

        bool isReduce() { return body.ty == Body::Reduce; }
    };

    void testArray(Func f) {
        string sexp = f.serialize();
        cerr << sexp << endl;
        rehydrate(sexp, f.name());
    }

    Func rehydrateFunc(map<string, Definition>& defs,
                       map<string, Func>& env,
                       const string func);

    Expr rehydrateExpr(map<string, Definition>& defs,
                       map<string, Func>& env,
                       set<string> curArgs,
                       MLVal expr)
    {
        Expr e(expr, typeOfExpr(expr));

        cerr << "rehydrateExpr: " << e.pretty() << endl;

        // Track dependences
        for (MLVal list = varsInExpr(expr); !listEmpty(list); list = listTail(list)) {
            MLVal var = listHead(list);
            string name = var[0];
            Type t = var[1];
            if (curArgs.count(name)) {
                // This is a simple free variable
                e.child(Var(name));
                cerr << "  var: " << name << endl;
            } else {
                // This is a uniform
                // TODO: handle uniforms!
                cerr << "  uniform: " << name << endl;
                e.child(DynUniform(t, name));
            }
        }

        for (MLVal list = callsInExpr(expr); !listEmpty(list); list = listTail(list)) {
            MLVal call = listHead(list);
            string name = call[0];
            Type ret = call[1][1];
            if (callIsFunc(call[1][0])) {
                Func f = rehydrateFunc(defs, env, name);
                e.child(FuncRef(f));
            } else if (callIsImage(call[1][0])) {
                // TODO: figure out dimensionality of call
            }
        }

        return e;
    }

    Func rehydrateFunc(map<string,
                       Definition>& defs,
                       map<string,
                       Func>& env,
                       const string func)
    {
        // If we've already rehydrated this, return it
        if (env.count(func)) return env[func];

        cerr << "Rehydrating " << func << endl;

        // Build a new FuncContents from the definition
        Definition& def = defs[func];
        FuncContents *c = new FuncContents(def.name, def.ret_t);
        assert(!def.isReduce()); // TODO: this isn't handled yet
        
        set<string> args;
        for (size_t i = 0; i < def.args.size(); i++) {
            string arg = def.args[i].first;
            c->args.push_back(Var(arg));
            args.insert(arg);
        }

        // Rehydrate the rhs Expr
        c->rhs = rehydrateExpr(defs, env, args, def.body.rhs);

        // Add it to the environment
        env[func] = c->toFunc();

        return env[func];
    }

    Func rehydrate(const string sexp, const string rootFunc) {
        map<string, Func> env;

        map<string, Definition> defs;
        for (MLVal list = getEnvDefinitions(deserializeEnv(sexp));
             !listEmpty(list); list = listTail(list)) {
            Definition def(listHead(list));
            defs[def.name] = def;
            cerr << def.name << " is " << (def.isReduce() ? "" : "not ") << "reduce" << endl;
        }

        return rehydrateFunc(defs, env, rootFunc);
    }

}