// DRNC-Q QPC target backend. Compiles the DRNC-Q source AST to OpenQASM 3.0.
// OpenQASM is an IR; physical submission requires a compatible local QPU driver/control stack.
class QPCEmitter {
    struct Wire { int base{0}; int size{1}; bool array{false}; bool bellPair{false}; };
    struct CReg { int base{0}; int size{1}; };
    struct TeleportInfo { int aux{-1}; int msrc{-1}; int mbell{-1}; };
    const SourceProgram& src_;
    std::map<std::string,Wire> wires_;
    std::map<std::string,CReg> classicalBits_;
    std::map<std::string,std::string> classicalTypes_;
    std::map<std::string,std::string> constants_;
    std::map<std::string,int> declaredLines_;
    std::map<const Expr*,TeleportInfo> teleport_;
    std::set<std::string> quantumOps_;
    std::map<std::string,const FunctionDef*> opDefs_;
    std::vector<double> energyGaps_;
    std::vector<std::string> body_;
    int nq_{0}, nc_{0}, indent_{0};

    static std::string trim(const std::string& s) {
        auto a=s.find_first_not_of(" \t\r\n"); if(a==std::string::npos)return {};
        auto b=s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
    }
    static std::string number(double x) {
        if(!std::isfinite(x))throw std::runtime_error("QPC compiler: non-finite numeric expression");
        std::ostringstream o; o<<std::setprecision(17)<<x; return o.str();
    }
    [[noreturn]] void fail(const std::string& msg,int line=0) const {
        throw std::runtime_error("QPC target"+(line?" line "+std::to_string(line):std::string())+": "+msg);
    }
    void line(const std::string& s) { body_.push_back(std::string(static_cast<std::size_t>(indent_)*2,' ')+s); }
    static bool call_is(const E& e,const std::string& name) { return e&&e->kind==Expr::Kind::Call&&e->text==name; }
    bool is_number(const E& e) const { return e&&e->kind==Expr::Kind::Num; }
    int numeric_int(const E& e,const std::string& ctx) const {
        if(!is_number(e)||e->num<0||std::floor(e->num)!=e->num||e->num>1024)fail(ctx+" must be a compile-time non-negative integer",e?e->line:0);
        return static_cast<int>(e->num);
    }
    bool wire_constructor(const E& e) const {
        return call_is(e,"qregfonk")||call_is(e,"qubitfonk")||call_is(e,"superposfonk")||call_is(e,"donfonk")||call_is(e,"zeryafonk");
    }
    int allocate_wire(int n,bool array,bool pair,const std::string& name,bool branch=false) {
        if(n<1||n>256)fail("QPC logical qubit allocation must be in 1..256 (actual target limits may be smaller)");
        if(wires_.count(name)||classicalBits_.count(name)||classicalTypes_.count(name))fail("duplicate DRNCQ binding '"+name+"'");
        int base=nq_; nq_+=n; wires_[name]=Wire{base,n,array,pair};
        for(int i=0;i<n;++i)energyGaps_.push_back(branch?(-2.0*SQRT3):(SQRT2-1.0));
        return base;
    }
    int resolve_qubit(const E& e) const {
        if(!e)fail("missing qubit operand");
        if(e->kind==Expr::Kind::Var){auto it=wires_.find(e->text);if(it==wires_.end())fail("'"+e->text+"' is not a known quantum binding",e->line);if(it->second.array)fail("register '"+e->text+"' requires an index (for example q[0])",e->line);return it->second.base;}
        if(e->kind==Expr::Kind::Index&&e->left&&e->left->kind==Expr::Kind::Var&&is_number(e->right)){
            auto it=wires_.find(e->left->text);if(it==wires_.end())fail("unknown quantum register '"+e->left->text+"'",e->line);
            int ix=numeric_int(e->right,"qubit index");if(ix>=it->second.size)fail("qubit index out of bounds for '"+e->left->text+"'",e->line);return it->second.base+ix;
        }
        if(call_is(e,"partner")){
            if(e->args.size()!=1)fail("partner expects one qubit",e->line);const E& a=e->args[0];
            if(a->kind==Expr::Kind::Var){auto it=wires_.find(a->text);if(it==wires_.end()||it->second.size!=2)fail("partner requires a qubit in a two-qubit DRNC register",e->line);if(it->second.array)fail("use partner(q[0]) for an indexed register",e->line);return it->second.base+1;}
            if(a->kind==Expr::Kind::Index&&a->left&&a->left->kind==Expr::Kind::Var&&is_number(a->right)){
                auto it=wires_.find(a->left->text);if(it==wires_.end()||it->second.size!=2)fail("partner requires a qubit in a two-qubit register",e->line);int ix=numeric_int(a->right,"partner index");if(ix>1)fail("partner index must be 0 or 1",e->line);return it->second.base+(1-ix);
            }
            fail("partner requires a named two-qubit register or indexed register qubit",e->line);
        }
        fail("QPC quantum operands must be named DRNCQ bindings or constant-index qubits",e->line);
    }
    CReg resolve_register(const E& e) const {
        if(!e)fail("missing register operand");
        if(e->kind==Expr::Kind::Var){auto it=wires_.find(e->text);if(it==wires_.end())fail("unknown quantum binding '"+e->text+"'",e->line);return CReg{it->second.base,it->second.size};}
        if(e->kind==Expr::Kind::Index&&e->left&&e->left->kind==Expr::Kind::Var){auto it=wires_.find(e->left->text);if(it==wires_.end())fail("unknown quantum register",e->line);return CReg{resolve_qubit(e),1};}
        fail("expected a named quantum register",e->line);
    }
    bool constant_value(const E& e,double& out) const {
        if(!e)return false;
        if(e->kind==Expr::Kind::Num){out=e->num;return true;}
        if(e->kind==Expr::Kind::Unary){double a=0;if(!constant_value(e->left,a))return false;if(e->text=="-")out=-a;else if(e->text=="+")out=a;else return false;return std::isfinite(out);}
        if(e->kind==Expr::Kind::Call&&e->text=="pi"&&e->args.empty()){out=PI;return true;}
        if(e->kind==Expr::Kind::Var){auto it=constants_.find(e->text);if(it!=constants_.end()){char* end=nullptr;out=std::strtod(it->second.c_str(),&end);return end&&*end=='\0'&&std::isfinite(out);}return false;}
        if(e->kind==Expr::Kind::Binary){double a=0,b=0;if(!constant_value(e->left,a)||!constant_value(e->right,b))return false;if(e->text=="+")out=a+b;else if(e->text=="-")out=a-b;else if(e->text=="*")out=a*b;else if(e->text=="/"){if(std::abs(b)<EPS)return false;out=a/b;}else if(e->text=="^")out=std::pow(a,b);else return false;return std::isfinite(out);}
        return false;
    }
    static std::string qasm_op(const std::string& op) {
        if(op=="==")return "==";if(op=="!=")return "!=";if(op=="<")return "<";if(op=="<=")return "<=";if(op==">")return ">";if(op==">=")return ">=";
        if(op=="&&")return "&&";if(op=="||")return "||";if(op=="^")return "**";return op;
    }
    std::string classical_expr(const E& e) const {
        if(!e)fail("missing classical expression");
        switch(e->kind){
          case Expr::Kind::Num:return number(e->num);
          case Expr::Kind::Bool:return e->boolean?"true":"false";
          case Expr::Kind::Var:{auto c=classicalBits_.find(e->text);if(c!=classicalBits_.end()){if(c->second.size!=1)fail("measurement binding '"+e->text+"' is a bit array",e->line);return "c["+std::to_string(c->second.base)+"]";}if(classicalTypes_.count(e->text)||constants_.count(e->text)||activeAngleSymbols_.count(e->text))return e->text;fail("classical expression uses unsupported or undeclared variable '"+e->text+"'",e->line);}
          case Expr::Kind::Unary:return "("+e->text+classical_expr(e->left)+")";
          case Expr::Kind::Binary:{
            // DRNCQ measurement functions return EVET/HAYIR labels. In QPC these map to physical result bits 0/1.
            if((e->text=="=="||e->text=="!=")&&e->left&&e->right){
                const E* var=nullptr;const E* str=nullptr;if(e->left->kind==Expr::Kind::Var&&e->right->kind==Expr::Kind::Str){var=&e->left;str=&e->right;}else if(e->right->kind==Expr::Kind::Var&&e->left->kind==Expr::Kind::Str){var=&e->right;str=&e->left;}
                if(var&&str&&classicalBits_.count((*var)->text)){
                    const auto& lab=(*str)->text;int bit=-1;if(lab=="EVET"||lab=="DAL+")bit=0;else if(lab=="HAYIR"||lab=="DAL-")bit=1;
                    if(bit<0)fail("measurement labels in QPC conditions must be EVET/HAYIR or DAL+/DAL-",e->line);
                    return "(c["+std::to_string(classicalBits_.at((*var)->text).base)+"] "+e->text+" "+std::to_string(bit)+")";
                }
            }
            return "("+classical_expr(e->left)+" "+qasm_op(e->text)+" "+classical_expr(e->right)+")";
          }
          case Expr::Kind::Call:{
            if(e->text=="pi"&&e->args.empty())return "pi";
            static const std::set<std::string> math={"sqrt","sin","cos","tan","exp","log","log10","floor","ceil","abs","pow","min","max"};
            if(math.count(e->text)){std::string s=e->text=="log"?"ln":(e->text=="log10"?"log10":e->text);std::string out=s+"(";for(std::size_t i=0;i<e->args.size();++i){if(i)out+=", ";out+=classical_expr(e->args[i]);}return out+")";}
            fail("function '"+e->text+"' is not a supported QPC compile-time/classical expression",e->line);
          }
          case Expr::Kind::Str:fail("string values are only permitted as DRNCQ sector labels or gate names in QPC mode",e->line);
          default:fail("unsupported QPC classical expression",e->line);
        }
    }
    std::string qubit_expr(const E& e) const { return "q["+std::to_string(resolve_qubit(e))+"]"; }
    std::string gate_name(const std::string& n) const {
        if(n=="hgate")return "h";if(n=="xgate")return "x";if(n=="ygate")return "y";if(n=="zgate")return "z";if(n=="sgate")return "s";if(n=="tgate")return "t";
        if(n=="cxgate")return "cx";if(n=="czgate")return "cz";if(n=="swapgate")return "swap";if(n=="ccxgate"||n=="toffoligate")return "ccx";
        if(n=="phasegate")return "p";if(n=="rxgate")return "rx";if(n=="rygate")return "ry";if(n=="rzgate")return "rz";if(n=="cphasegate")return "cp";
        if(quantumOps_.count(n))return n;
        fail("unsupported gate or operation '"+n+"'");
    }
    std::string gate_call_string(const std::string& f,const std::vector<E>& args,const std::map<std::string,std::string>* paramMap=nullptr) const {
        auto qarg=[&](const E& e)->std::string{
            if(paramMap&&e&&e->kind==Expr::Kind::Var){auto it=paramMap->find(e->text);if(it!=paramMap->end())return it->second;}
            return qubit_expr(e);
        };
        auto argAngle=[&](const E& e)->std::string{return classical_expr(e);};
        std::string name=gate_name(f), out;
        if(f=="hgate"||f=="xgate"||f=="ygate"||f=="zgate"||f=="sgate"||f=="tgate"){
            if(args.size()!=1)fail(f+" expects one qubit operand",args.empty()?0:args[0]->line);out=name+" "+qarg(args[0])+";";
        } else if(f=="phasegate"||f=="rxgate"||f=="rygate"||f=="rzgate"){
            if(args.size()!=2)fail(f+" expects a qubit and an angle");out=name+"("+argAngle(args[1])+") "+qarg(args[0])+";";
        } else if(f=="cxgate"||f=="czgate"||f=="swapgate"){
            if(args.size()!=2)fail(f+" expects two qubits");out=name+" "+qarg(args[0])+", "+qarg(args[1])+";";
        } else if(f=="cphasegate"){
            if(args.size()!=3)fail("cphasegate expects two qubits and an angle");out="cp("+argAngle(args[2])+") "+qarg(args[0])+", "+qarg(args[1])+";";
        } else if(f=="ccxgate"||f=="toffoligate"){
            if(args.size()!=3)fail(f+" expects three qubits");out="ccx "+qarg(args[0])+", "+qarg(args[1])+", "+qarg(args[2])+";";
        } else if(quantumOps_.count(f)){
            const auto* def=opDefs_.at(f);
            if(args.size()!=def->params.size())fail("operation '"+f+"' expects "+std::to_string(def->params.size())+" argument(s) in source-signature order");
            std::vector<std::string> angles,qubits;
            for(std::size_t i=0;i<def->params.size();++i){
                const auto& param=def->params[i];
                if(gate_angle_params_.at(f).count(param))angles.push_back(argAngle(args[i]));
                else qubits.push_back(qarg(args[i]));
            }
            out=name;
            if(!angles.empty()){out+="(";for(std::size_t i=0;i<angles.size();++i){if(i)out+=", ";out+=angles[i];}out+=")";}
            out+=" ";for(std::size_t i=0;i<qubits.size();++i){if(i)out+=", ";out+=qubits[i];}out+=";";
        } else fail("not a QPC gate call: "+f);
        return out;
    }
    std::map<std::string,std::set<std::string>> gate_angle_params_;
    std::set<std::string> activeAngleSymbols_;
    void preallocate_expr(const E& e) {
        if(!e)return;
        if(e->kind==Expr::Kind::Call&&e->text=="teleportfonk"){
            if(e->args.size()!=2)fail("teleportfonk(source,destination) expects two qubit references",e->line);
            TeleportInfo ti;ti.aux=nq_++;energyGaps_.push_back(SQRT2-1.0);ti.msrc=nc_++;ti.mbell=nc_++;teleport_[e.get()]=ti;return;
        }
        for(const auto& a:e->args)preallocate_expr(a);preallocate_expr(e->left);preallocate_expr(e->right);
    }
    void preallocate_stmt(const S& s) {
        if(!s)return;
        if(s->kind==Stmt::Kind::Decl){
            const E& e=s->expr;
            if(call_is(e,"qregfonk")){
                if(e->args.size()!=1)fail("qregfonk expects one compile-time integer",e->line);int n=numeric_int(e->args[0],"qregfonk size");if(n<1||n>64)fail("QPC register size must be in 1..64 (target hardware may impose a lower limit)",e->line);allocate_wire(n,true,false,s->name);return;
            }
            if(call_is(e,"qubitfonk")||call_is(e,"superposfonk")) { if(call_is(e,"qubitfonk")&&!e->args.empty())fail("qubitfonk takes no arguments",e->line);if(call_is(e,"superposfonk")&&e->args.size()>1)fail("superposfonk accepts at most one phase",e->line);allocate_wire(1,false,false,s->name);return; }
            if(call_is(e,"donfonk")&&e->args.empty()){allocate_wire(2,false,true,s->name);return;}
            if(call_is(e,"zeryafonk")){if(e->args.size()!=1)fail("zeryafonk expects one source qubit",e->line);allocate_wire(1,false,false,s->name,true);return;}
            if(call_is(e,"measure")){if(e->args.size()!=1)fail("measure expects one qubit",e->line);if(classicalBits_.count(s->name)||wires_.count(s->name))fail("duplicate measurement binding '"+s->name+"'",e->line);classicalBits_[s->name]=CReg{nc_++,1};preallocate_expr(e);return;}
            if(call_is(e,"measure_basis")){if(e->args.size()!=2)fail("measure_basis expects qubit,basis",e->line);if(classicalBits_.count(s->name)||wires_.count(s->name))fail("duplicate measurement binding '"+s->name+"'",e->line);classicalBits_[s->name]=CReg{nc_++,1};preallocate_expr(e);return;}
            if(call_is(e,"measure_all")){if(e->args.size()!=1)fail("measure_all expects a register",e->line);CReg r=resolve_register(e->args[0]);if(classicalBits_.count(s->name)||wires_.count(s->name))fail("duplicate measurement binding",e->line);classicalBits_[s->name]=CReg{nc_,r.size};nc_+=r.size;preallocate_expr(e);return;}
            if(call_is(e,"partner")){int ix=resolve_qubit(e);wires_[s->name]=Wire{ix,1,false,false};return;}
            // Aliasing a known qubit/register is supported; arbitrary runtime quantum expressions are rejected.
            if(e&&e->kind==Expr::Kind::Var&&wires_.count(e->text)){if(wires_.count(s->name))fail("duplicate quantum binding",s->line);wires_[s->name]=wires_.at(e->text);return;}
            if(e&&e->kind==Expr::Kind::Index){int ix=resolve_qubit(e);wires_[s->name]=Wire{ix,1,false,false};return;}
            if(call_is(e,"teleportfonk"))fail("assign teleportfonk through a statement so the destination can be rebound",e->line);
            if(e&&e->kind==Expr::Kind::Call){
                double v=0; if(e->text=="pi"&&e->args.empty())v=PI;else if(constant_value(e,v)){}else fail("DRNCQ initializers in QPC mode must be qubit constructors, measurement results, or compile-time classical values",e->line);
            }
            if(e&&e->kind==Expr::Kind::Str)fail("arbitrary string values are not representable in QPC circuit code",e->line);
            if(e&&e->kind==Expr::Kind::Bool)classicalTypes_[s->name]="bool";
            else {double v=0;if(constant_value(e,v))classicalTypes_[s->name]=(std::floor(v)==v?"int":"float");else if(e&&e->kind!=Expr::Kind::Call)fail("unsupported QPC variable initializer",s->line);else if(e) {if(e->text=="pi"||e->text=="sqrt"||e->text=="sin"||e->text=="cos"||e->text=="tan"||e->text=="exp"||e->text=="log"||e->text=="log10"||e->text=="abs"||e->text=="pow"||e->text=="min"||e->text=="max")classicalTypes_[s->name]="float";else fail("unsupported QPC initializer call '"+e->text+"'",e->line);}}
            return;
        }
        if(s->kind==Stmt::Kind::Block)for(const auto& c:s->children)preallocate_stmt(c);
        else if(s->kind==Stmt::Kind::If){preallocate_expr(s->cond);preallocate_stmt(s->thenStmt);preallocate_stmt(s->elseStmt);}
        else if(s->kind==Stmt::Kind::While){preallocate_expr(s->cond);preallocate_stmt(s->body);}
        else if(s->kind==Stmt::Kind::For){preallocate_stmt(s->init);preallocate_expr(s->cond);preallocate_expr(s->inc);preallocate_stmt(s->body);}
        else if(s->kind==Stmt::Kind::Expr)preallocate_expr(s->expr);
        else if(s->kind==Stmt::Kind::Assign)preallocate_expr(s->expr);
        else if(s->kind==Stmt::Kind::TryCatch||s->kind==Stmt::Kind::Throw)fail("try/catch/throw is not supported in a physical QPU circuit target",s->line);
    }
    void emit_measure(const std::string& var,const E& call) {
        if(call_is(call,"measure")){line("c["+std::to_string(classicalBits_.at(var).base)+"] = measure "+qubit_expr(call->args[0])+";");return;}
        if(call_is(call,"measure_basis")){
            const E& q=call->args[0];if(call->args[1]->kind!=Expr::Kind::Str)fail("measure_basis basis must be a string literal",call->line);const std::string b=call->args[1]->text;std::string qw=qubit_expr(q),cb="c["+std::to_string(classicalBits_.at(var).base)+"]";
            if(b=="X"){line("h "+qw+";");line(cb+" = measure "+qw+";");line("h "+qw+";");}
            else if(b=="Y"){line("sdg "+qw+";");line("h "+qw+";");line(cb+" = measure "+qw+";");line("h "+qw+";");line("s "+qw+";");}
            else if(b=="Z")line(cb+" = measure "+qw+";");else fail("measurement basis must be X, Y, or Z",call->line);return;
        }
        if(call_is(call,"measure_all")){CReg r=resolve_register(call->args[0]);auto cr=classicalBits_.at(var);if(cr.size!=r.size)fail("measurement result size changed during lowering",call->line);for(int i=0;i<r.size;++i)line("c["+std::to_string(cr.base+i)+"] = measure q["+std::to_string(r.base+i)+"];");return;}
        fail("unsupported measurement constructor",call?call->line:0);
    }
    std::string gate_name_from_string(const E& e) const { if(!e||e->kind!=Expr::Kind::Str)fail("gate modifier requires a string-literal operation name",e?e->line:0);return e->text; }
    void emit_quantum_call(const E& e) {
        const std::string& f=e->text;const auto& a=e->args;
        if(f=="teleportfonk") { emit_teleport(e);return; }
        if(f=="moduleradim"||f=="evrimfonk") {
            double ds=1.0;
            if(f=="moduleradim"){
                if(a.size()>1)fail("moduleradim accepts at most one argument",e->line);
                if(!a.empty()&&!constant_value(a[0],ds))fail("QPC modular step must be compile-time constant",e->line);
            } else {
                if(a.empty()||a.size()>2)fail("evrimfonk expects q[,ds]",e->line);
                (void)resolve_qubit(a[0]);
                if(a.size()==2&&!constant_value(a[1],ds))fail("QPC modular step must be compile-time constant",e->line);
            }
            if(!std::isfinite(ds)||std::abs(ds)>1e6)fail("modular increment is outside allowed range",e->line);
            // Modular flow is global: evrimfonk validates its relational anchor, then advances
            // the same shared modular parameter used by moduleradim and evolves every live sector.
            for(int i=0;i<nq_;++i){double gap=energyGaps_.at(static_cast<std::size_t>(i));line("p("+number(-gap*ds)+") q["+std::to_string(i)+"];");}
            return;
        }
        if(f=="controlledgate") {
            if(a.size()<3||a.size()>4)fail("controlledgate(name, control, target-or-[targets][,angle-or-[angles]]) expects 3 or 4 arguments",e->line);
            std::string name=gate_name_from_string(a[0]);int ctrl=resolve_qubit(a[1]);
            std::vector<int> targets;
            if(a[2]&&a[2]->kind==Expr::Kind::List){if(a[2]->args.empty())fail("controlledgate target list cannot be empty",e->line);for(const auto& t:a[2]->args)targets.push_back(resolve_qubit(t));}
            else targets.push_back(resolve_qubit(a[2]));
            std::set<int> uniq(targets.begin(),targets.end());if(uniq.size()!=targets.size()||uniq.count(ctrl))fail("controlledgate operands must be distinct",e->line);
            std::vector<std::string> angles;
            if(quantumOps_.count(name)){
                const auto* def=opDefs_.at(name);std::size_t qcount=0;for(const auto& p:def->params)if(!gate_angle_params_.at(name).count(p))++qcount;
                const std::size_t acount=gate_angle_params_.at(name).size();
                if(targets.size()!=qcount)fail("controlled custom operation '"+name+"' needs "+std::to_string(qcount)+" target qubit(s)",e->line);
                if(acount==0&&a.size()!=3)fail("controlled custom operation '"+name+"' has no angle parameters",e->line);
                if(acount>0&&a.size()!=4)fail("controlled custom operation '"+name+"' requires angle argument(s)",e->line);
                if(acount==1&&a.size()==4&&a[3]->kind!=Expr::Kind::List)angles.push_back(classical_expr(a[3]));
                else if(acount>0&&a.size()==4&&a[3]->kind==Expr::Kind::List){for(const auto& x:a[3]->args)angles.push_back(classical_expr(x));}
                else if(acount>0&&a.size()==4&&acount>1)fail("multiple custom gate angles must be passed as a list",e->line);
                else if(acount>1)fail("multiple custom gate angles must be passed as a list",e->line);
                if(angles.size()!=acount)fail("controlled custom operation angle count mismatch",e->line);
                std::string out="ctrl @ "+name;
                if(!angles.empty()){out+="(";for(std::size_t i=0;i<angles.size();++i){if(i)out+=", ";out+=angles[i];}out+=")";}
                out+=" q["+std::to_string(ctrl)+"]";for(int t:targets)out+=", q["+std::to_string(t)+"]";line(out+";");return;
            }
            const bool rotation=(name=="phasegate"||name=="rxgate"||name=="rygate"||name=="rzgate");
            if(a[2]->kind==Expr::Kind::List){if(a.size()!=3)fail("built-in multi-target controlledgate does not accept an extra angle",e->line);}
            else if(rotation){if(a.size()!=4)fail("controlled rotation requires fourth angle argument",e->line);angles.push_back(classical_expr(a[3]));}
            else if(a.size()!=3)fail("non-rotation built-in controlledgate accepts exactly three arguments",e->line);
            std::string g=gate_name(name),out="ctrl @ "+g;
            if(!angles.empty())out+="("+angles.front()+")";
            out+=" q["+std::to_string(ctrl)+"]";for(int t:targets)out+=", q["+std::to_string(t)+"]";line(out+";");return;
        }
        if(f=="adjointgate") {
            if(a.size()<2)fail("adjointgate requires a gate name and operands",e->line);
            std::string name=gate_name_from_string(a[0]);std::vector<E> operands(a.begin()+1,a.end());
            if(quantumOps_.count(name)){
                std::string call=gate_call_string(name,operands);if(call.empty()||call.back()!=';')fail("internal custom adjoint formatting error",e->line);call.pop_back();line("inv @ "+call+";");return;
            }
            if(name=="cxgate"||name=="czgate"||name=="swapgate"||name=="ccxgate"||name=="toffoligate"){line(gate_call_string(name,operands));return;}
            if(operands.size()<1||operands.size()>2)fail("adjoint single-qubit gate expects q[,angle]",e->line);
            std::string gn=gate_name(name);
            if(name=="phasegate"||name=="rxgate"||name=="rygate"||name=="rzgate"){if(operands.size()!=2)fail("adjoint rotation requires an angle",e->line);line("inv @ "+gn+"("+classical_expr(operands[1])+") "+qubit_expr(operands[0])+";");}
            else{if(operands.size()!=1)fail("gate takes one operand",e->line);line("inv @ "+gn+" "+qubit_expr(operands[0])+";");}return;
        }
        if(f=="powergate") {
            if(a.size()<3||a.size()>4)fail("powergate(name, q-or-[qubits], power[,angle-or-[angles]]) expects 3 or 4 arguments",e->line);
            std::string name=gate_name_from_string(a[0]);double raw=0;if(!constant_value(a[2],raw)||std::floor(raw)!=raw||raw< -32||raw>32)fail("powergate exponent must be a compile-time integer in [-32,32]",e->line);
            int n=static_cast<int>(raw),count=std::abs(n);std::vector<E> qargs;if(a[1]&&a[1]->kind==Expr::Kind::List)qargs=a[1]->args;else qargs.push_back(a[1]);
            if(qargs.empty())fail("powergate target list cannot be empty",e->line);
            std::vector<E> callArgs;
            if(quantumOps_.count(name)){
                const auto* def=opDefs_.at(name);std::vector<std::string> angleParams,qubitParams;
                for(const auto& p:def->params)(gate_angle_params_.at(name).count(p)?angleParams:qubitParams).push_back(p);
                if(qargs.size()!=qubitParams.size())fail("powergate custom operation requires "+std::to_string(qubitParams.size())+" qubit operand(s)",e->line);
                std::vector<E> angleArgs;
                if(angleParams.empty()){if(a.size()!=3)fail("custom operation has no angle parameters",e->line);}
                else {
                    if(a.size()!=4)fail("parameterized custom operation requires angle argument(s)",e->line);
                    if(angleParams.size()==1&&a[3]->kind!=Expr::Kind::List)angleArgs.push_back(a[3]);
                    else if(a[3]->kind==Expr::Kind::List)angleArgs=a[3]->args;
                    else fail("multiple custom gate angles must be passed as a list",e->line);
                    if(angleArgs.size()!=angleParams.size())fail("powergate custom angle count mismatch",e->line);
                }
                std::size_t ai=0,qi=0;for(const auto& param:def->params)if(gate_angle_params_.at(name).count(param))callArgs.push_back(angleArgs.at(ai++));else callArgs.push_back(qargs.at(qi++));
            } else {
                callArgs=qargs;if(a.size()==4)callArgs.push_back(a[3]);
            }
            for(int i=0;i<count;++i){
                if(n<0){
                    if(name=="cxgate"||name=="czgate"||name=="swapgate"||name=="ccxgate"||name=="toffoligate")line(gate_call_string(name,callArgs));
                    else if(quantumOps_.count(name)){std::string call=gate_call_string(name,callArgs);call.pop_back();line("inv @ "+call+";");}
                    else {if(callArgs.size()<1||callArgs.size()>2)fail("single-qubit inverse gate has invalid operand count",e->line);line("inv @ "+gate_name(name)+(callArgs.size()==2?"("+classical_expr(callArgs[1])+")":"")+" "+qubit_expr(callArgs[0])+";");}
                } else line(gate_call_string(name,callArgs));
            }
            return;
        }
        if(f=="mcxgate"||f=="mcphasegate"||f=="mczgate"){
            if(a.size()<(f=="mcphasegate"?3u:2u))fail(f+" has too few arguments",e->line);std::vector<int> cs;
            if(a[0]->kind==Expr::Kind::List){for(const auto& c:a[0]->args)cs.push_back(resolve_qubit(c));}else cs.push_back(resolve_qubit(a[0]));if(cs.empty())fail(f+" requires at least one control",e->line);int t=resolve_qubit(a[1]);std::set<int> uniq(cs.begin(),cs.end());if(uniq.size()!=cs.size()||uniq.count(t))fail("multi-controlled operands must be distinct",e->line);std::string mod="ctrl"+(cs.size()>1?"("+std::to_string(cs.size())+")":std::string())+" @ ";std::string base=f=="mcxgate"?"x":(f=="mczgate"?"z":"p("+classical_expr(a[2])+")");std::string out=mod+base+" ";for(int c:cs)out+="q["+std::to_string(c)+"], ";out+="q["+std::to_string(t)+"];";line(out);return;
        }
        if(f=="measure")fail("measure() must be assigned to a DRNCQ variable so its classical result is available",e->line);
        if(f=="measure_all")fail("measure_all() must be assigned to a DRNCQ variable",e->line);
        if(f=="measure_basis")fail("measure_basis() must be assigned to a DRNCQ variable",e->line);
        if(f=="resetqubit"){if(a.size()!=1)fail("resetqubit expects one qubit",e->line);line("reset "+qubit_expr(a[0])+";");return;}
        if(f=="barrier"){if(a.size()!=1)fail("barrier expects one operand",e->line);CReg r=resolve_register(a[0]);std::string b="barrier ";for(int i=0;i<r.size;++i){if(i)b+=", ";b+="q["+std::to_string(r.base+i)+"]";}line(b+";");return;}
        if(f=="hgate"||f=="xgate"||f=="ygate"||f=="zgate"||f=="sgate"||f=="tgate"||f=="phasegate"||f=="rxgate"||f=="rygate"||f=="rzgate"||f=="cxgate"||f=="czgate"||f=="swapgate"||f=="cphasegate"||f=="ccxgate"||f=="toffoligate") { line(gate_call_string(f,a));return; }
        if(quantumOps_.count(f)){line(gate_call_string(f,a));return;}
        if(f=="donfonk"){if(a.size()!=2)fail("donfonk() is a state constructor; as a statement use donfonk(qA,qB)",e->line);line("h "+qubit_expr(a[0])+";");line("cx "+qubit_expr(a[0])+", "+qubit_expr(a[1])+";");return;}
        if(f=="zeryafonk"){fail("zeryafonk() is a constructor; assign it with DRNCQ = \"branch\", zeryafonk(source)",e->line);}
        if(f=="superposfonk"||f=="qubitfonk"||f=="qregfonk")fail(f+" is a constructor and must be bound with DRNCQ",e->line);
        fail("function '"+f+"' is not a hardware circuit operation; diagnostics, print and simulator noise functions are PC-target only",e->line);
    }
    void emit_teleport(const E& e) {
        auto it=teleport_.find(e.get());if(it==teleport_.end())fail("internal teleportation allocation mismatch",e->line);if(e->args.size()!=2)fail("teleportfonk expects source,destination",e->line);int src=resolve_qubit(e->args[0]),dst=resolve_qubit(e->args[1]);if(src==dst)fail("teleport source and destination must differ",e->line);auto t=it->second;int aux=t.aux;
        line("h q["+std::to_string(dst)+"];");line("cx q["+std::to_string(dst)+"], q["+std::to_string(aux)+"];");line("cx q["+std::to_string(src)+"], q["+std::to_string(dst)+"];");line("h q["+std::to_string(src)+"];");line("c["+std::to_string(t.msrc)+"] = measure q["+std::to_string(src)+"];");line("c["+std::to_string(t.mbell)+"] = measure q["+std::to_string(dst)+"];");line("if (c["+std::to_string(t.mbell)+"] == 1) { x q["+std::to_string(aux)+"]; }");line("if (c["+std::to_string(t.msrc)+"] == 1) { z q["+std::to_string(aux)+"]; }");
        if(e->args[1]->kind!=Expr::Kind::Var)fail("teleportfonk destination must be a named DRNCQ variable that can be rebound",e->line);const std::string& dstName=e->args[1]->text;wires_[dstName]=Wire{aux,1,false,false};
    }
    std::string gate_body_stmt(const S& st,const std::map<std::string,std::string>& pmap,const std::string& gateName) const {
        if(!st)return {};
        if(st->kind==Stmt::Kind::Block){std::string out;for(const auto& c:st->children)out+=gate_body_stmt(c,pmap,gateName);return out;}
        if(st->kind!=Stmt::Kind::Expr||!st->expr||st->expr->kind!=Expr::Kind::Call)fail("operation '"+gateName+"' must contain only unitary gate calls; measurements/control flow are forbidden in gate definitions",st->line);
        const auto& e=st->expr; const auto& a=e->args; auto qname=[&](const E& x)->std::string{if(!x||x->kind!=Expr::Kind::Var||!pmap.count(x->text))fail("operation body qubit operands must be operation parameters",x?x->line:e->line);return pmap.at(x->text);};
        std::string nm=gate_name(e->text),text;
        if(e->text=="hgate"||e->text=="xgate"||e->text=="ygate"||e->text=="zgate"||e->text=="sgate"||e->text=="tgate"){if(a.size()!=1)fail("wrong gate arity in operation",e->line);text=nm+" "+qname(a[0])+";\n";}
        else if(e->text=="phasegate"||e->text=="rxgate"||e->text=="rygate"||e->text=="rzgate"){if(a.size()!=2)fail("wrong rotation arity in operation",e->line);text=nm+"("+classical_expr(a[1])+") "+qname(a[0])+";\n";}
        else if(e->text=="cxgate"||e->text=="czgate"||e->text=="swapgate"){if(a.size()!=2)fail("wrong controlled gate arity in operation",e->line);text=nm+" "+qname(a[0])+", "+qname(a[1])+";\n";}
        else if(e->text=="cphasegate"){if(a.size()!=3)fail("wrong cphasegate arity in operation",e->line);text="cp("+classical_expr(a[2])+") "+qname(a[0])+", "+qname(a[1])+";\n";}
        else if(e->text=="ccxgate"||e->text=="toffoligate"){if(a.size()!=3)fail("wrong Toffoli arity in operation",e->line);text="ccx "+qname(a[0])+", "+qname(a[1])+", "+qname(a[2])+";\n";}
        else if(quantumOps_.count(e->text)){auto it=opDefs_.find(e->text);if(it==opDefs_.end())fail("undefined custom operation "+e->text,e->line);text=gate_call_string(e->text,a,&pmap)+"\n";}
        else fail("operation body uses unsupported operation '"+e->text+"'",e->line);
        std::string out;std::istringstream iss(text);std::string lineText;while(std::getline(iss,lineText))if(!lineText.empty())out+=std::string(static_cast<std::size_t>(indent_)*2+2,' ')+lineText+"\n";return out;
    }
    void infer_expr_uses(const E& e,const FunctionDef& f,std::set<std::string>& qu,std::set<std::string>& au,bool angleContext=false) {
        if(!e)return;
        if(e->kind==Expr::Kind::Var&&std::find(f.params.begin(),f.params.end(),e->text)!=f.params.end()){
            (angleContext?au:qu).insert(e->text);return;
        }
        if(e->kind==Expr::Kind::Call){
            const auto& n=e->text;
            if(n=="phasegate"||n=="rxgate"||n=="rygate"||n=="rzgate"){
                if(!e->args.empty())infer_expr_uses(e->args[0],f,qu,au,false);
                if(e->args.size()>1)infer_expr_uses(e->args[1],f,qu,au,true);return;
            }
            if(n=="cphasegate"){
                if(e->args.size()>1){infer_expr_uses(e->args[0],f,qu,au,false);infer_expr_uses(e->args[1],f,qu,au,false);}
                if(e->args.size()>2)infer_expr_uses(e->args[2],f,qu,au,true);return;
            }
            if(n=="controlledgate"){
                for(std::size_t i=1;i<e->args.size();++i)infer_expr_uses(e->args[i],f,qu,au,i==3);return;
            }
            if(n=="adjointgate"||n=="powergate"){
                for(std::size_t i=1;i<e->args.size();++i)infer_expr_uses(e->args[i],f,qu,au,(n=="powergate"&&i==2));return;
            }
            if(n=="mcphasegate"){
                for(std::size_t i=0;i<e->args.size();++i)infer_expr_uses(e->args[i],f,qu,au,i==2);return;
            }
            if(quantumOps_.count(n)){
                auto defIt=opDefs_.find(n);if(defIt==opDefs_.end())fail("undefined custom operation '"+n+"'",e->line);
                auto angleIt=gate_angle_params_.find(n);
                if(angleIt==gate_angle_params_.end())fail("custom operation '"+n+"' must be declared before it is used inside another operation",e->line);
                const auto* callee=defIt->second;
                if(e->args.size()!=callee->params.size())fail("custom operation '"+n+"' argument count mismatch",e->line);
                for(std::size_t i=0;i<callee->params.size();++i){
                    const bool angle=angleIt->second.count(callee->params[i])!=0;
                    infer_expr_uses(e->args[i],f,qu,au,angle);
                }
                return;
            }
        }
        if(e->kind==Expr::Kind::Binary){infer_expr_uses(e->left,f,qu,au,angleContext);infer_expr_uses(e->right,f,qu,au,angleContext);return;}
        for(const auto& a:e->args)infer_expr_uses(a,f,qu,au,angleContext);
        infer_expr_uses(e->left,f,qu,au,angleContext);infer_expr_uses(e->right,f,qu,au,angleContext);
    }
    void infer_stmt_uses(const S& s,const FunctionDef& f,std::set<std::string>& qu,std::set<std::string>& au) {
        if(!s)return;if(s->kind==Stmt::Kind::Block){for(const auto& c:s->children)infer_stmt_uses(c,f,qu,au);return;}if(s->kind==Stmt::Kind::Expr){infer_expr_uses(s->expr,f,qu,au);return;}fail("operation definition may contain only gate-expression statements",s->line);
    }
    void prepare_ops() {
        for(const auto& f:src_.functions)if(f.quantumOperation){if(!opDefs_.emplace(f.name,&f).second)fail("duplicate quantum operation '"+f.name+"'",f.line);quantumOps_.insert(f.name);}
        // Infer parameter kinds in source order. Custom gates may call prior operations,
        // but forward references are rejected so emitted OpenQASM definitions are ordered.
        for(const auto& f:src_.functions)if(f.quantumOperation){
            std::set<std::string> qu,au;infer_stmt_uses(f.body,f,qu,au);
            for(const auto& p:f.params){bool q=qu.count(p)!=0,a=au.count(p)!=0;if(q==a)fail("operation parameter '"+p+"' must be used consistently as either a qubit or an angle",f.line);if(q&&a)fail("operation parameter cannot be both a qubit and an angle",f.line);if(a)gate_angle_params_[f.name].insert(p);}
        }
    }
    void emit_operation_defs(std::ostringstream& out) {
        // User operations are emitted in source order; a custom operation may call only a prior definition.
        std::set<std::string> already;
        for(const auto& f:src_.functions)if(f.quantumOperation){
            std::vector<std::string> angles,qubits;for(const auto& p:f.params)(gate_angle_params_[f.name].count(p)?angles:qubits).push_back(p);
            std::map<std::string,std::string> pmap;for(const auto& q:qubits)pmap[q]=q;
            out<<"gate "<<f.name;if(!angles.empty()){out<<"(";for(std::size_t i=0;i<angles.size();++i){if(i)out<<", ";out<<angles[i];}out<<")";}for(const auto& q:qubits)out<<" "<<q;out<<" {\n";
            int oldIndent=indent_;indent_=1;const auto oldAngles=activeAngleSymbols_;activeAngleSymbols_=gate_angle_params_.at(f.name);std::string body=gate_body_stmt(f.body,pmap,f.name);activeAngleSymbols_=oldAngles;indent_=oldIndent;out<<body<<"}\n";already.insert(f.name);
        }
    }
    void collect_teleports_expr(const E& e) { if(!e)return;if(e->kind==Expr::Kind::Call&&e->text=="teleportfonk"){if(teleport_.count(e.get()))return;TeleportInfo t;t.aux=nq_++;energyGaps_.push_back(SQRT2-1.0);t.msrc=nc_++;t.mbell=nc_++;teleport_[e.get()]=t;return;}for(const auto& a:e->args)collect_teleports_expr(a);collect_teleports_expr(e->left);collect_teleports_expr(e->right); }
    void allocate_stmt(const S& s) {
        if(!s)return;
        if(s->kind==Stmt::Kind::Decl){const E& e=s->expr;
            if(wire_constructor(e)||call_is(e,"measure")||call_is(e,"measure_basis")||call_is(e,"measure_all")||call_is(e,"partner")) {preallocate_stmt(s);return;}
            if(e&&e->kind==Expr::Kind::Var&&wires_.count(e->text)){wires_[s->name]=wires_.at(e->text);return;}
            if(e&&e->kind==Expr::Kind::Index){wires_[s->name]=Wire{resolve_qubit(e),1,false,false};return;}
            if(e&&e->kind==Expr::Kind::Call&&e->text=="teleportfonk")fail("teleportfonk cannot initialize a binding; call it as a statement so destination rebinding is explicit",e->line);
            if(wires_.count(s->name)||classicalBits_.count(s->name)||classicalTypes_.count(s->name))fail("duplicate DRNCQ binding '"+s->name+"'",s->line);
            if(e&&e->kind==Expr::Kind::Bool)classicalTypes_[s->name]="bool";
            else {double v=0;if(constant_value(e,v)){classicalTypes_[s->name]=(std::floor(v)==v?"int":"float");constants_[s->name]=number(v);}else if(e&&e->kind==Expr::Kind::Call&&(e->text=="pi"||e->text=="sqrt"||e->text=="sin"||e->text=="cos"||e->text=="tan"||e->text=="exp"||e->text=="log"||e->text=="log10"||e->text=="abs"||e->text=="pow"||e->text=="min"||e->text=="max"))classicalTypes_[s->name]="float";else fail("QPC DRNCQ values must be qubit constructors, measurements, numeric/bool classical values or aliases",s->line);}
            return;
        }
        if(s->kind==Stmt::Kind::Expr) {collect_teleports_expr(s->expr);return;}
        if(s->kind==Stmt::Kind::Block){for(const auto& c:s->children)allocate_stmt(c);return;}
        if(s->kind==Stmt::Kind::If){allocate_stmt(s->thenStmt);allocate_stmt(s->elseStmt);return;}
        if(s->kind==Stmt::Kind::While){allocate_stmt(s->body);return;}
        if(s->kind==Stmt::Kind::For){allocate_stmt(s->init);allocate_stmt(s->body);return;}
        if(s->kind==Stmt::Kind::Assign){return;}
        if(s->kind==Stmt::Kind::TryCatch||s->kind==Stmt::Kind::Throw)fail("exceptions are compile-time/host concepts and are not permitted in QPC real-time code",s->line);
    }
    void emit_block(const S& s) {
        if(!s)return;
        switch(s->kind){
          case Stmt::Kind::Block:for(const auto& c:s->children)emit_block(c);break;
          case Stmt::Kind::Decl:{const E& e=s->expr;
            if(call_is(e,"qregfonk")||call_is(e,"qubitfonk"))break;
            if(call_is(e,"superposfonk")){int q=wires_.at(s->name).base;line("h q["+std::to_string(q)+"];");if(!e->args.empty()){double phase=0;if(!constant_value(e->args[0],phase))fail("superposfonk phase must be a compile-time constant for QPC",e->line);line("p("+number(phase)+") q["+std::to_string(q)+"];");}break;}
            if(call_is(e,"donfonk")&&e->args.empty()){int q=wires_.at(s->name).base;line("h q["+std::to_string(q)+"];");line("cx q["+std::to_string(q)+"], q["+std::to_string(q+1)+"];");break;}
            if(call_is(e,"zeryafonk")){int control=resolve_qubit(e->args[0]),target=wires_.at(s->name).base;line("cx q["+std::to_string(control)+"], q["+std::to_string(target)+"];");break;}
            if(call_is(e,"measure")||call_is(e,"measure_basis")||call_is(e,"measure_all")){emit_measure(s->name,e);break;}
            if(call_is(e,"partner")|| (e&&e->kind==Expr::Kind::Var&&wires_.count(e->text)) || (e&&e->kind==Expr::Kind::Index))break;
            if(classicalBits_.count(s->name))break;
            if(classicalTypes_.count(s->name)){const std::string typ=classicalTypes_.at(s->name);if(typ=="bool")line("bool "+s->name+" = "+classical_expr(e)+";");else line(std::string(typ=="int"?"int":"float")+" "+s->name+" = "+classical_expr(e)+";");if(e&&e->kind==Expr::Kind::Num)constants_[s->name]=number(e->num);else if(e&&e->kind==Expr::Kind::Call&&e->text=="pi")constants_[s->name]=number(PI);break;}
            fail("unsupported DRNCQ initializer for QPC",s->line);
          }
          case Stmt::Kind::Expr:{const E& e=s->expr;if(e&&e->kind==Expr::Kind::Call){emit_quantum_call(e);break;}fail("only gate, measurement-derived operation and teleport calls can be stand-alone QPC expressions",s->line);}
          case Stmt::Kind::Assign:{if(wires_.count(s->name)){if(s->expr&&s->expr->kind==Expr::Kind::Call&&s->expr->text=="partner"){int ix=resolve_qubit(s->expr);wires_[s->name]=Wire{ix,1,false,false};break;}fail("quantum handles cannot be reassigned after QPC allocation except teleportfonk destination",s->line);}if(!classicalTypes_.count(s->name))fail("assignment to unknown QPC classical variable '"+s->name+"'",s->line);line(s->name+" = "+classical_expr(s->expr)+";");break;}
          case Stmt::Kind::If:line("if ("+classical_expr(s->cond)+") {");indent_++;emit_block(s->thenStmt);indent_--;if(s->elseStmt){line("} else {");indent_++;emit_block(s->elseStmt);indent_--;}line("}");break;
          case Stmt::Kind::While:line("while ("+classical_expr(s->cond)+") {");indent_++;emit_block(s->body);indent_--;line("}");break;
          case Stmt::Kind::For:{std::string init;if(s->init&&s->init->kind==Stmt::Kind::Assign)init=s->init->name+" = "+classical_expr(s->init->expr);else if(s->init&&s->init->kind==Stmt::Kind::Expr)init=classical_expr(s->init->expr);else fail("unsupported QPC for initializer",s->line);std::string inc;if(s->inc&&s->inc->kind==Expr::Kind::Call&&s->inc->text=="__assign"&&s->inc->args.size()==2&&s->inc->args[0]->kind==Expr::Kind::Str)inc=s->inc->args[0]->text+" = "+classical_expr(s->inc->args[1]);else inc=classical_expr(s->inc);line("for ("+init+"; "+classical_expr(s->cond)+"; "+inc+") {");indent_++;emit_block(s->body);indent_--;line("}");break;}
          case Stmt::Kind::Break:line("break;");break;
          case Stmt::Kind::Continue:line("continue;");break;
          case Stmt::Kind::Return:fail("return is not valid at circuit top level",s->line);
          case Stmt::Kind::TryCatch:case Stmt::Kind::Throw:fail("try/catch/throw is unavailable in QPC real-time circuit mode",s->line);
          case Stmt::Kind::IndexAssign:case Stmt::Kind::MemberAssign:fail("list/object mutation is host-language-only in QPC mode",s->line);
        }
    }
public:
    explicit QPCEmitter(const SourceProgram& src):src_(src){}
    std::string emit() {
        if(src_.target!="QPC")fail("source must begin with %set[QPC]");
        prepare_ops();
        for(const auto& s:src_.statements)allocate_stmt(s);
        if(nq_==0)fail("QPC program allocates no qubits");
        // Add measurement bits for teleportation calls found in nested blocks after declarations have mapped operands.
        for(const auto& s:src_.statements)collect_teleports_stmt(s);
        if(nq_>256)fail("program exceeds portable QPC logical limit of 256 qubits");if(nc_>1024)fail("program exceeds portable QPC classical-bit limit of 1024");
        std::ostringstream out;out<<"OPENQASM 3.0;\ninclude \"stdgates.inc\";\n";
        emit_operation_defs(out);
        out<<"\nqubit["<<nq_<<"] q;\n";if(nc_>0)out<<"bit["<<nc_<<"] c;\n\n";
        for(const auto& s:src_.statements)emit_block(s);
        for(const auto& text:body_)out<<text<<"\n";
        out<<"// DRNC-Q QPC lowering: EVET/HAYIR map to physical computational |0>/|1>.\n";
        out<<"// DRNC modular evolution is lowered to relative phase gates; global phases are omitted.\n";
        out<<"// Hardware-native mapping, qubit topology, pulse calibration and job submission belong to the local QPU driver.\n";
        return out.str();
    }
private:
    void collect_teleports_stmt(const S& s){if(!s)return;if(s->kind==Stmt::Kind::Expr)collect_teleports_expr(s->expr);else if(s->kind==Stmt::Kind::Block)for(const auto& c:s->children)collect_teleports_stmt(c);else if(s->kind==Stmt::Kind::If){collect_teleports_expr(s->cond);collect_teleports_stmt(s->thenStmt);collect_teleports_stmt(s->elseStmt);}else if(s->kind==Stmt::Kind::While){collect_teleports_expr(s->cond);collect_teleports_stmt(s->body);}else if(s->kind==Stmt::Kind::For){collect_teleports_stmt(s->init);collect_teleports_stmt(s->body);collect_teleports_expr(s->inc);}}
};
static std::string qpc_compile_file(const std::string& path) {
    SourceProgram s;std::vector<std::filesystem::path> visiting;std::set<std::filesystem::path> loaded;load_module_tree(path,visiting,loaded,s,true);
    if(s.target!="QPC")throw std::runtime_error("QPC compiler requires %set[QPC] at the start of the source file");
    QPCEmitter qpc(s);return qpc.emit();
}
