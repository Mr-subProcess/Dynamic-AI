// DRNC-Q v1.8 — PC/QPC compiler, DRNQB7 VM, QPC analysis and conservative optimizer.
// C++17 only. No Python, external quantum SDK, cloud API, or provider runtime.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace drncq {
using C = std::complex<double>;
constexpr double PI = 3.141592653589793238462643383279502884;
constexpr double SQRT2 = 1.414213562373095048801688724209698079;
constexpr double SQRT3 = 1.732050807568877293527446341505872884;
constexpr double EPS = 1e-10;
constexpr std::size_t MAX_SOURCE = 8u * 1024u * 1024u;
constexpr std::uint32_t MAX_INSTRUCTIONS = 1000000u;
constexpr int MAX_QUBITS = 16; // State-vector limit: 2^16 complex amplitudes per register.
constexpr std::uint64_t MAX_VM_STEPS = 2000000ull;

static std::string read_text(const std::string& path) {
    std::ifstream f(path,std::ios::binary); if(!f) throw std::runtime_error("cannot open '"+path+"'");
    f.seekg(0,std::ios::end); auto n=f.tellg(); if(n<0 || static_cast<std::uint64_t>(n)>MAX_SOURCE) throw std::runtime_error("source exceeds 8 MiB limit");
    f.seekg(0); std::string s(static_cast<std::size_t>(n),'\0'); if(!s.empty()&&!f.read(s.data(),n)) throw std::runtime_error("cannot read source"); return s;
}

struct TargetHeader { std::string target{"PC"}; std::string body; bool explicitTarget{false}; };
static TargetHeader extract_target_header(std::string source) {
    TargetHeader out; out.body=std::move(source);
    std::size_t pos=0;
    while(pos<out.body.size()) {
        std::size_t end=out.body.find('\n',pos); if(end==std::string::npos) end=out.body.size();
        std::size_t a=pos,b=end; while(a<b&&(out.body[a]==' '||out.body[a]=='\t'||out.body[a]=='\r'))++a;
        while(b>a&&(out.body[b-1]==' '||out.body[b-1]=='\t'||out.body[b-1]=='\r'))--b;
        std::string line=out.body.substr(a,b-a);
        if(line.empty()||line.rfind("#",0)==0||line.rfind("//",0)==0){pos=(end<out.body.size()?end+1:end);continue;}
        if(line=="%set[PC]"||line=="%set[QPC]"){
            out.target=(line=="%set[PC]")?"PC":"QPC";out.explicitTarget=true;
            for(std::size_t i=pos;i<end;++i)out.body[i]=' ';
            return out;
        }
        if(line.rfind("%set[",0)==0) throw std::runtime_error("invalid target selector '"+line+"'; expected %set[PC] or %set[QPC]");
        return out;
    }
    return out;
}
static std::string target_for_file(const std::string& path) {
    auto h=extract_target_header(read_text(path));
    if(!h.explicitTarget) throw std::runtime_error("missing target selector: first non-comment code line must be %set[PC] or %set[QPC]");
    return h.target;
}

// ------------------------------ Lexer --------------------------------------
enum class TK { End, Id, Number, String, Symbol };
struct Token { TK kind; std::string text; double number{0}; int line{1}; int col{1}; };
class Lexer {
public:
    explicit Lexer(const std::string& s):s_(s){}
    std::vector<Token> scan() {
        std::vector<Token> out;
        while (true) {
            skip(); if (p_>=s_.size()) { out.push_back({TK::End,"",0,line_,col_}); break; }
            const int ln=line_, cl=col_; char c=s_[p_];
            if (is_id_start(c)) {
                std::string x; while(p_<s_.size()&&is_id_part(s_[p_])) x.push_back(take());
                out.push_back({TK::Id,x,0,ln,cl});
            } else if (std::isdigit(static_cast<unsigned char>(c)) || (c=='.'&&p_+1<s_.size()&&std::isdigit(static_cast<unsigned char>(s_[p_+1])))) {
                std::size_t begin=p_; bool dot=false;
                if (c=='.') { dot=true; take(); }
                while(p_<s_.size()&&std::isdigit(static_cast<unsigned char>(s_[p_]))) take();
                if(!dot&&p_<s_.size()&&s_[p_]=='.'){dot=true;take();while(p_<s_.size()&&std::isdigit(static_cast<unsigned char>(s_[p_])))take();}
                if(p_<s_.size()&&(s_[p_]=='e'||s_[p_]=='E')) { take(); if(p_<s_.size()&&(s_[p_]=='+'||s_[p_]=='-'))take(); std::size_t ep=p_; while(p_<s_.size()&&std::isdigit(static_cast<unsigned char>(s_[p_])))take(); if(ep==p_) error(ln,cl,"bad exponent in number"); }
                std::string x=s_.substr(begin,p_-begin); char* end=nullptr; double v=std::strtod(x.c_str(),&end); if(!end||*end||!std::isfinite(v)) error(ln,cl,"numeric literal must be finite"); out.push_back({TK::Number,x,v,ln,cl});
            } else if(c=='"') {
                take(); std::string x; bool closed=false;
                while(p_<s_.size()) { char q=take(); if(q=='"'){closed=true;break;} if(q=='\n'||q=='\r') error(ln,cl,"newline in string literal"); if(q=='\\'){ if(p_>=s_.size())error(ln,cl,"unfinished escape"); char e=take(); switch(e){case 'n':x+='\n';break;case 't':x+='\t';break;case 'r':x+='\r';break;case '\\':x+='\\';break;case '"':x+='"';break;default:error(ln,cl,"unsupported string escape");}} else x+=q; }
                if(!closed) error(ln,cl,"unterminated string literal");
                out.push_back({TK::String,x,0,ln,cl});
            } else {
                std::string two; if(p_+1<s_.size()) two=s_.substr(p_,2);
                if(two=="=="||two=="!="||two=="<="||two==">="||two=="&&"||two=="||") { take();take();out.push_back({TK::Symbol,two,0,ln,cl}); }
                else if(std::string("{}[].(),;=+-*/%^!<> ").find(c)!=std::string::npos && c!=' ') { take(); out.push_back({TK::Symbol,std::string(1,c),0,ln,cl}); }
                else error(ln,cl,std::string("unexpected character '")+c+"'");
            }
            if(out.size()>MAX_INSTRUCTIONS*16ull) throw std::runtime_error("token limit exceeded");
        }
        return out;
    }
private:
    const std::string& s_; std::size_t p_{0}; int line_{1},col_{1};
    static bool is_id_start(char c){return std::isalpha(static_cast<unsigned char>(c))||c=='_';}
    static bool is_id_part(char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';}
    char take(){char c=s_[p_++]; if(c=='\n'){line_++;col_=1;}else col_++;return c;}
    void error(int l,int c,const std::string& m){throw std::runtime_error("line "+std::to_string(l)+":"+std::to_string(c)+": "+m);}
    void skip(){while(p_<s_.size()){char c=s_[p_]; if(c==' '||c=='\t'||c=='\r'||c=='\n'){take();continue;} if(c=='#'){while(p_<s_.size()&&s_[p_]!='\n')take();continue;} if(c=='/'&&p_+1<s_.size()&&s_[p_+1]=='/'){while(p_<s_.size()&&s_[p_]!='\n')take();continue;} break;}}
};

// ------------------------------ AST / Parser -------------------------------
struct Expr; struct Stmt;
using E=std::shared_ptr<Expr>; using S=std::shared_ptr<Stmt>;
struct Expr {
    enum class Kind { Num, Str, Bool, Null, Var, Unary, Binary, Call, List, Index, Member, MethodCall, NewObj } kind;
    int line{1}; double num{0}; bool boolean{false}; std::string text; E left,right; std::vector<E> args;
};
struct Stmt {
    enum class Kind { Decl, Assign, IndexAssign, MemberAssign, Expr, Block, If, While, For, Return, Break, Continue, TryCatch, Throw } kind;
    int line{1}; std::string name, catchName, memberName; E expr,cond,inc,indexExpr,objectExpr; S init,thenStmt,elseStmt,body,catchBody; std::vector<S> children;
};
// Parameter modes: 0=input (girdi), 1=output (cikti), 2=in/out (girdicikti).
struct FunctionDef { std::string name; std::vector<std::string> params; std::vector<std::uint8_t> modes; S body; int line{1}; bool quantumOperation{false}; };
struct ClassDef { std::string name; std::vector<FunctionDef> methods; int line{1}; };
struct SourceProgram {std::string name{"anonymous"}; std::string target{"PC"}; std::vector<S> statements; std::vector<FunctionDef> functions; std::vector<ClassDef> classes; std::vector<std::string> imports;};
class Parser {
public:
    explicit Parser(std::vector<Token> t):t_(std::move(t)){}
    SourceProgram parse(){
        SourceProgram p;
        if(match_id("program")){auto id=expect(TK::Id,"program name");p.name=id.text; optional(";");}
        while(!at(TK::End)) {
            if(match(";")) continue;
            if(match_id("import")) { auto f=expect(TK::String,"module path string"); expect_s(";"); p.imports.push_back(f.text); continue; }
            if(match_id("fn") || match_id("function") || match_id("fonk")) { p.functions.push_back(function_def(false)); continue; }
            if(match_id("operation") || match_id("kuantumfonk")) { p.functions.push_back(function_def(true)); continue; }
            if(match_id("class")) { p.classes.push_back(class_def()); continue; }
            p.statements.push_back(statement());
            if(p.statements.size()+p.functions.size()>MAX_INSTRUCTIONS)fail(peek(),"program definition limit exceeded");
        }
        if(p.statements.empty() && p.functions.empty() && p.classes.empty() && p.imports.empty()) fail(peek(),"empty source program");
        return p;
    }
private:
    std::vector<Token> t_; std::size_t p_{0};
    const Token& peek(std::size_t n=0)const{return t_.at(std::min(p_+n,t_.size()-1));}
    bool at(TK k)const{return peek().kind==k;}
    bool is(const std::string& x)const{return peek().text==x;}
    bool match(const std::string& x){if(is(x)){p_++;return true;}return false;}
    bool match_id(const std::string& x){if(at(TK::Id)&&is(x)){p_++;return true;}return false;}
    bool optional(const std::string& x){return match(x);}
    Token consume(){return t_.at(p_++);}
    Token expect(TK k,const std::string& what){if(!at(k))fail(peek(),"expected "+what+", got '"+peek().text+"'");return consume();}
    void expect_s(const std::string& x){if(!match(x))fail(peek(),"expected '"+x+"'");}
    [[noreturn]] void fail(const Token& t,const std::string& m){throw std::runtime_error("line "+std::to_string(t.line)+":"+std::to_string(t.col)+": "+m);}
    FunctionDef function_def(bool quantumOperation=false){
        Token n=expect(TK::Id,"function name"); FunctionDef f; f.name=n.text; f.line=n.line; f.quantumOperation=quantumOperation; expect_s("(");
        if(!match(")")){
            do {
                std::uint8_t mode=0;
                if(match_id("girdi")||match_id("in")) mode=0;
                else if(match_id("cikti")||match_id("out")) mode=1;
                else if(match_id("girdicikti")||match_id("inout")) mode=2;
                auto a=expect(TK::Id,"parameter name");
                if(std::find(f.params.begin(),f.params.end(),a.text)!=f.params.end())fail(a,"duplicate function parameter");
                f.params.push_back(a.text); f.modes.push_back(mode);
            } while(match(","));
            expect_s(")");
        }
        f.body=block_required(); return f;
    }
    ClassDef class_def(){
        Token n=expect(TK::Id,"class name"); ClassDef c; c.name=n.text; c.line=n.line; expect_s("{");
        while(!match("}")){ if(at(TK::End)) fail(peek(),"unterminated class definition"); if(match(";")) continue; if(!(match_id("fn")||match_id("function")||match_id("fonk")||match_id("operation")||match_id("kuantumfonk"))) fail(peek(),"class bodies may contain function/operation methods only"); auto method=function_def(); if(std::any_of(method.modes.begin(),method.modes.end(),[](std::uint8_t m){return m!=0;})) fail(peek(),"class methods currently support input parameters only; use a top-level fonk for output parameters"); c.methods.push_back(std::move(method)); }
        return c;
    }
    S statement(){
        const Token start=peek();
        if(match_id("DRNCQ")) { expect_s("="); auto n=expect(TK::String,"quoted variable name"); if(n.text.empty()||!(std::isalpha(static_cast<unsigned char>(n.text[0]))||n.text[0]=='_')||!std::all_of(n.text.begin(),n.text.end(),[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';})) fail(n,"DRNCQ variable name must also be a valid identifier when referenced"); expect_s(","); S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Decl;s->line=start.line;s->name=n.text;s->expr=expression();expect_s(";");return s; }
        if(match_id("return")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Return;s->line=start.line;if(!is(";"))s->expr=expression();expect_s(";");return s;}
        if(match_id("break")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Break;s->line=start.line;expect_s(";");return s;}
        if(match_id("continue")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Continue;s->line=start.line;expect_s(";");return s;}
        if(match_id("throw")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Throw;s->line=start.line;s->expr=expression();expect_s(";");return s;}
        if(match_id("try")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::TryCatch;s->line=start.line;s->body=block_required();if(!match_id("catch"))fail(peek(),"expected catch after try block");expect_s("(");s->catchName=expect(TK::Id,"catch variable name").text;expect_s(")");s->catchBody=block_required();return s;}
        if(match_id("if")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::If;s->line=start.line;expect_s("(");s->cond=expression();expect_s(")");s->thenStmt=block_required();if(match_id("else")){if(is("if"))s->elseStmt=statement();else s->elseStmt=block_required();}return s;}
        if(match_id("while")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::While;s->line=start.line;expect_s("(");s->cond=expression();expect_s(")");s->body=block_required();return s;}
        if(match_id("for")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::For;s->line=start.line;expect_s("(");s->init=for_clause();expect_s(";");s->cond=expression();expect_s(";");s->inc=for_assignment_expr();expect_s(")");s->body=block_required();return s;}
        if(match("{")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Block;s->line=start.line;while(!match("}")){if(at(TK::End))fail(peek(),"unterminated block");if(match(";"))continue;s->children.push_back(statement());}return s;}
        if(at(TK::Id)&&peek(1).text=="=") {Token n=consume();consume();S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Assign;s->line=start.line;s->name=n.text;s->expr=expression();expect_s(";");return s;}
        if(at(TK::Id)&&peek(1).text==".") {std::size_t save=p_;E obj=std::make_shared<Expr>();obj->kind=Expr::Kind::Var;obj->line=peek().line;obj->text=consume().text;consume();Token field=expect(TK::Id,"property name");if(match("=")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::MemberAssign;s->line=start.line;s->objectExpr=obj;s->memberName=field.text;s->expr=expression();expect_s(";");return s;}p_=save;}
        if(at(TK::Id)&&peek(1).text=="[") {std::size_t save=p_;Token n=consume();consume();E ix=expression();expect_s("]");if(match("=")){S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::IndexAssign;s->line=start.line;s->name=n.text;s->indexExpr=ix;s->expr=expression();expect_s(";");return s;} p_=save;}
        S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Expr;s->line=start.line;s->expr=expression();expect_s(";");return s;
    }
    S block_required(){if(!is("{"))fail(peek(),"control-flow body must use a '{ ... }' block");return statement();}
    S for_clause(){Token tok=peek();if(at(TK::Id)&&peek(1).text=="="){Token n=consume();consume();S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Assign;s->line=tok.line;s->name=n.text;s->expr=expression();return s;}S s=std::make_shared<Stmt>();s->kind=Stmt::Kind::Expr;s->line=tok.line;s->expr=expression();return s;}
    E for_assignment_expr(){Token tok=peek();if(at(TK::Id)&&peek(1).text=="="){Token n=consume();consume();E rhs=expression();E e=std::make_shared<Expr>();e->kind=Expr::Kind::Call;e->line=tok.line;e->text="__assign";auto name=std::make_shared<Expr>();name->kind=Expr::Kind::Str;name->line=tok.line;name->text=n.text;e->args={name,rhs};return e;}return expression();}
    E expression(){return parse_binary(1);}
    int prec(const std::string& op)const{if(op=="||")return 1;if(op=="&&")return 2;if(op=="=="||op=="!=")return 3;if(op=="<"||op=="<="||op==">"||op==">=")return 4;if(op=="+"||op=="-")return 5;if(op=="*"||op=="/"||op=="%")return 6;if(op=="^")return 7;return 0;}
    E parse_binary(int minp){E lhs=unary();while(true){int pr=prec(peek().text);if(pr<minp)break;Token op=consume();E rhs=parse_binary(pr+(op.text=="^"?0:1));E e=std::make_shared<Expr>();e->kind=Expr::Kind::Binary;e->line=op.line;e->text=op.text;e->left=lhs;e->right=rhs;lhs=e;}return lhs;}
    E unary(){Token t=peek();if(match("!")||match("-")||match("+")){E e=std::make_shared<Expr>();e->kind=Expr::Kind::Unary;e->line=t.line;e->text=t.text;e->left=(t.text=="!")?unary():parse_binary(7);return e;}return primary();}
    E primary(){Token t=peek(); E out;
        if(match("(")){out=expression();expect_s(")");}
        else if(match("[")){out=std::make_shared<Expr>();out->kind=Expr::Kind::List;out->line=t.line;if(!match("]")){do{out->args.push_back(expression());}while(match(","));expect_s("]");}}
        else if(at(TK::Number)){consume();out=std::make_shared<Expr>();out->kind=Expr::Kind::Num;out->line=t.line;out->num=t.number;}
        else if(at(TK::String)){consume();out=std::make_shared<Expr>();out->kind=Expr::Kind::Str;out->line=t.line;out->text=t.text;}
        else if(match_id("true")||match_id("false")){out=std::make_shared<Expr>();out->kind=Expr::Kind::Bool;out->line=t.line;out->boolean=(t.text=="true");}
        else if(match_id("null")){out=std::make_shared<Expr>();out->kind=Expr::Kind::Null;out->line=t.line;}
        else if(match_id("new")){Token cls=expect(TK::Id,"class name after new");out=std::make_shared<Expr>();out->kind=Expr::Kind::NewObj;out->line=t.line;out->text=cls.text;expect_s("(");if(!match(")")){do{out->args.push_back(expression());}while(match(","));expect_s(")");}}
        else if(at(TK::Id)){consume();if(match("(")){out=std::make_shared<Expr>();out->kind=Expr::Kind::Call;out->line=t.line;out->text=t.text;if(!match(")")){do{out->args.push_back(expression());}while(match(","));expect_s(")");}}else{out=std::make_shared<Expr>();out->kind=Expr::Kind::Var;out->line=t.line;out->text=t.text;}}
        else fail(t,"expected expression");
        while(true){
            if(match("[")){E ix=expression();expect_s("]");E e=std::make_shared<Expr>();e->kind=Expr::Kind::Index;e->line=t.line;e->left=out;e->right=ix;out=e;continue;}
            if(match(".")){Token m=expect(TK::Id,"member name");if(match("(")){E e=std::make_shared<Expr>();e->kind=Expr::Kind::MethodCall;e->line=t.line;e->text=m.text;e->left=out;if(!match(")")){do{e->args.push_back(expression());}while(match(","));expect_s(")");}out=e;}else{E e=std::make_shared<Expr>();e->kind=Expr::Kind::Member;e->line=t.line;e->text=m.text;e->left=out;out=e;}continue;}
            break;
        }
        return out;
    }
};

static void load_module_tree(const std::filesystem::path& path,std::vector<std::filesystem::path>& visiting,std::set<std::filesystem::path>& loaded,SourceProgram& out,bool isRoot);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include "qpc_backend.inc"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// -------------------------- Bytecode compiler ------------------------------
enum class BO : std::uint8_t {
    PushNum,PushStr,PushBool,PushNull,Load,Declare,Store,Pop,
    Add,Sub,Mul,Div,Mod,Pow,Pos,Neg,Not,Eq,Ne,Lt,Le,Gt,Ge,ToBool,
    Call,CallUser,Jump,JumpFalse,Halt,MakeList,IndexGet,IndexSet,
    Return,EnterScope,ExitScope,TryBegin,TryEnd,Throw,MemberGet,MemberSet,CallMethod,CallClass
};
struct BI {BO op{BO::Halt};std::uint32_t line{0};double num{0};bool boolean{false};std::string text;std::uint32_t a{0};std::vector<std::string> refs;};
struct FunctionInfo {std::vector<std::string> params;std::vector<std::uint8_t> modes;std::uint32_t entry{0};};
struct ByteProgram {std::string name;std::vector<BI> code;std::map<std::string,FunctionInfo> functions;std::set<std::string> classes;};
class Compiler {
public:
    ByteProgram compile(const SourceProgram& s){
        src_=s; functions_.clear(); classes_.clear(); globals_.clear(); loop_stack_.clear(); scopeDepth_=0;
        for(const auto& cl:s.classes){if(keyword_or_builtin(cl.name)||builtin(cl.name)||!classes_.insert(cl.name).second)throw std::runtime_error("line "+std::to_string(cl.line)+": invalid or duplicate class name '"+cl.name+"'");}
        for(const auto& f:s.functions){if(builtin(f.name)||keyword_or_builtin(f.name)||classes_.count(f.name))throw std::runtime_error("line "+std::to_string(f.line)+": function name '"+f.name+"' is reserved");if(functions_.count(f.name))throw std::runtime_error("line "+std::to_string(f.line)+": duplicate function '"+f.name+"'");functions_[f.name]=f;}
        for(const auto& cl:s.classes){for(const auto& m:cl.methods){if(m.params.empty()||m.params.front()!="self")throw std::runtime_error("line "+std::to_string(m.line)+": class methods must take 'self' as the first parameter");FunctionDef mf=m;mf.name=cl.name+"."+m.name;if(functions_.count(mf.name))throw std::runtime_error("line "+std::to_string(m.line)+": duplicate method '"+mf.name+"'");functions_[mf.name]=mf;}}
        for(const auto& st:s.statements)if(st->kind==Stmt::Kind::Decl){if(keyword_or_builtin(st->name)||functions_.count(st->name)||classes_.count(st->name))throw std::runtime_error("line "+std::to_string(st->line)+": variable name '"+st->name+"' is reserved");if(!globals_.insert(st->name).second)throw std::runtime_error("line "+std::to_string(st->line)+": duplicate global DRNCQ declaration '"+st->name+"'");}
        std::vector<std::set<std::string>> noScopes;
        for(const auto& st:s.statements)validate_stmt(st,noScopes,false,0);
        for(const auto& entry:functions_){const auto& f=entry.second;std::vector<std::set<std::string>> scopes(1);for(const auto& a:f.params){if(keyword_or_builtin(a))throw std::runtime_error("line "+std::to_string(f.line)+": parameter name '"+a+"' is reserved");if(!scopes.back().insert(a).second)throw std::runtime_error("line "+std::to_string(f.line)+": duplicate parameter '"+a+"'");}validate_stmt(f.body,scopes,true,0);}
        ByteProgram p;p.name=s.name;p.classes=classes_;
        auto jump=emit(p.code,BO::Jump,0,{},0,0);
        for(const auto& fn: functions_){const auto& f=fn.second;FunctionInfo info;info.params=f.params;info.modes=f.modes;if(info.modes.size()!=info.params.size())throw std::runtime_error("internal function parameter-mode mismatch");if(p.code.size()>UINT32_MAX)throw std::runtime_error("function entry overflow");info.entry=static_cast<std::uint32_t>(p.code.size());p.functions[fn.first]=info;emit_stmt(f.body,p.code,0,true);emit(p.code,BO::PushNull,f.line);emit(p.code,BO::Return,f.line,{},0,0);}
        patch(p.code,jump,p.code.size());
        for(const auto& st:s.statements)emit_stmt(st,p.code,0,false);
        emit(p.code,BO::Halt,0);
        if(p.code.size()>MAX_INSTRUCTIONS)throw std::runtime_error("compiled instruction limit exceeded");
        return p;
    }
private:
    SourceProgram src_;std::map<std::string,FunctionDef> functions_;std::set<std::string> classes_,globals_;
    struct LoopPatch {std::vector<std::size_t> breaks,continues;int scopeDepth{0};};std::vector<LoopPatch> loop_stack_;int scopeDepth_{0};
    static bool builtin(const std::string& f){static const std::set<std::string> names={"print","yaz","type","pi","abs","sqrt","sin","cos","tan","asin","acos","atan","exp","log","log10","floor","ceil","round","pow","min","max","show_basis","modular_status","moduleradim","superposfonk","qubitfonk","qregfonk","donfonk","partner","zeryafonk","evrimfonk","norm","assert_normalized","verify_entanglement","measure","measure_all","probability","probabilities","expectation_z","dump_state","resetqubit","register_size","teleportfonk","same_register","__assign","len","push","pop","get","set","to_number","to_string","input","read_file","write_file","file_exists","upper","lower","contains","substr","split","join","assert","hgate","xgate","ygate","zgate","sgate","tgate","phasegate","rxgate","rygate","rzgate","cxgate","czgate","swapgate","cphasegate","ccxgate","toffoligate","seed","random","randint","sample_counts","bloch_vector","entanglement_entropy","bit_flip_noise","phase_flip_noise","depolarizing_noise","phase_damping_noise","amplitude_damping","controlledgate","adjointgate","powergate","mcxgate","mcphasegate","mczgate","measure_basis","expectation_x","expectation_y","expectation_pauli","statevector","density_matrix","reduced_density","purity","fidelity","barrier"};return names.count(f)!=0;}
    static bool keyword_or_builtin(const std::string& n){static const std::set<std::string> names={"DRNCQ","modular_s","__assign","program","if","else","for","while","true","false","null","fn","function","fonk","operation","kuantumfonk","girdi","cikti","girdicikti","in","out","inout","class","new","return","break","continue","try","catch","throw","import","print","yaz","type","pi","abs","sqrt","sin","cos","tan","asin","acos","atan","exp","log","log10","floor","ceil","round","pow","min","max","show_basis","modular_status","moduleradim","superposfonk","qubitfonk","qregfonk","donfonk","partner","zeryafonk","evrimfonk","norm","assert_normalized","verify_entanglement","measure","measure_all","probability","probabilities","expectation_z","dump_state","resetqubit","register_size","teleportfonk","same_register","len","push","pop","get","set","to_number","to_string","input","read_file","write_file","file_exists","upper","lower","contains","substr","split","join","assert","hgate","xgate","ygate","zgate","sgate","tgate","phasegate","rxgate","rygate","rzgate","cxgate","czgate","swapgate","cphasegate","ccxgate","toffoligate","seed","random","randint","sample_counts","bloch_vector","entanglement_entropy","bit_flip_noise","phase_flip_noise","depolarizing_noise","phase_damping_noise","amplitude_damping","controlledgate","adjointgate","powergate","mcxgate","mcphasegate","mczgate","measure_basis","expectation_x","expectation_y","expectation_pauli","statevector","density_matrix","reduced_density","purity","fidelity","barrier","operation","kuantumfonk"};return names.count(n)!=0;}
    bool name_known(const std::string& n,const std::vector<std::set<std::string>>& scopes)const{for(auto it=scopes.rbegin();it!=scopes.rend();++it)if(it->count(n))return true;return globals_.count(n)>0;}
    static std::string ref_descriptor(const E& e){
        if(!e)return {};
        if(e->kind==Expr::Kind::Var)return e->text;
        // Constant-index list references can be rebound when independent quantum registers merge.
        if(e->kind==Expr::Kind::Index&&e->left&&e->left->kind==Expr::Kind::Var&&e->right&&e->right->kind==Expr::Kind::Num&&e->right->num>=0&&std::floor(e->right->num)==e->right->num&&e->right->num<1000000)
            return e->left->text+"[#"+std::to_string(static_cast<std::uint64_t>(e->right->num))+"]";
        return {};
    }
    static bool is_quantum_reference_expr(const E& e){return e&&(e->kind==Expr::Kind::Var||(e->kind==Expr::Kind::Index&&e->left&&e->left->kind==Expr::Kind::Var));}
    void validate_expr(const E& e,const std::vector<std::set<std::string>>& scopes,bool inFunction){
        if(!e)return;
        if(e->kind==Expr::Kind::Var){if(e->text!="pi"&&e->text!="modular_s"&&!name_known(e->text,scopes))throw std::runtime_error("line "+std::to_string(e->line)+": undeclared variable '"+e->text+"'");}
        if(e->kind==Expr::Kind::Index){validate_expr(e->left,scopes,inFunction);validate_expr(e->right,scopes,inFunction);}
        if(e->kind==Expr::Kind::Member)validate_expr(e->left,scopes,inFunction);
        if(e->kind==Expr::Kind::MethodCall){validate_expr(e->left,scopes,inFunction);for(const auto& a:e->args)validate_expr(a,scopes,inFunction);}
        if(e->kind==Expr::Kind::NewObj){if(!classes_.count(e->text))throw std::runtime_error("line "+std::to_string(e->line)+": unknown class '"+e->text+"'");auto ctor=functions_.find(e->text+".init");std::size_t want=ctor==functions_.end()?0:(ctor->second.params.empty()?0:ctor->second.params.size()-1);if(e->args.size()!=want)throw std::runtime_error("line "+std::to_string(e->line)+": constructor expects "+std::to_string(want)+" argument(s)");for(const auto& a:e->args)validate_expr(a,scopes,inFunction);}
        if(e->kind==Expr::Kind::Call){const std::string& f=e->text; if(!builtin(f)&&!functions_.count(f)&&!classes_.count(f))throw std::runtime_error("line "+std::to_string(e->line)+": unknown function or class '"+f+"'");const std::size_t n=e->args.size();auto arity_error=[&](const std::string& ex){throw std::runtime_error("line "+std::to_string(e->line)+": "+f+" expects "+ex+" argument(s), got "+std::to_string(n));};
            if(functions_.count(f)&&functions_.at(f).params.size()!=n) arity_error(std::to_string(functions_.at(f).params.size()));
            if(functions_.count(f)){
                const auto& def=functions_.at(f);
                std::set<std::string> outputTargets;
                for(std::size_t k=0;k<n;++k) if(k<def.modes.size()&&def.modes[k]!=0){
                    const E& arg=e->args[k];
                    const std::string target=ref_descriptor(arg);
                    if(target.empty()) throw std::runtime_error("line "+std::to_string(e->line)+": output parameter '"+def.params[k]+"' of "+f+" needs a writable variable or constant-index list slot");
                    if(!outputTargets.insert(target).second) throw std::runtime_error("line "+std::to_string(e->line)+": output arguments of "+f+" must target distinct variables/list slots");
                    std::string base=arg->kind==Expr::Kind::Index?arg->left->text:arg->text;
                    if(!name_known(base,scopes)) throw std::runtime_error("line "+std::to_string(e->line)+": output argument '"+base+"' is not declared");
                }
            }
            if(classes_.count(f)){auto ctor=functions_.find(f+".init");std::size_t want=ctor==functions_.end()?0:(ctor->second.params.empty()?0:ctor->second.params.size()-1);if(n!=want)arity_error(std::to_string(want));}
            if((f=="pi"||f=="show_basis"||f=="modular_status"||f=="qubitfonk"||f=="random")&&n!=0)arity_error("0");
            if((f=="seed"||f=="randint"||f=="qregfonk"||f=="measure_all"||f=="probabilities"||f=="expectation_z"||f=="dump_state"||f=="resetqubit"||f=="register_size")&&n!=1)arity_error("1");
            if((f=="bloch_vector"||f=="entanglement_entropy")&&n!=1)arity_error("1");
            if((f=="bit_flip_noise"||f=="phase_flip_noise"||f=="depolarizing_noise"||f=="phase_damping_noise"||f=="amplitude_damping"||f=="sample_counts")&&n!=2)arity_error("2");
            if(f=="cphasegate"&&n!=3)arity_error("3");
            if(f=="mcxgate"&&n!=2)arity_error("2");
            if(f=="mcphasegate"&&n!=3)arity_error("3");
            if(f=="mczgate"&&n!=2)arity_error("2");
            if(f=="controlledgate"&&(n<3||n>4))arity_error("3 or 4");
            if(f=="adjointgate"&&(n<2||n>4))arity_error("2 to 4");
            if(f=="powergate"&&(n<3||n>4))arity_error("3 or 4");
            if(f=="expectation_pauli"&&n!=2)arity_error("2");
            if(f=="measure_basis"&&n!=2)arity_error("2");
            if((f=="expectation_x"||f=="expectation_y"||f=="statevector"||f=="density_matrix"||f=="reduced_density"||f=="purity"||f=="barrier")&&n!=1)arity_error("1");
            if(f=="fidelity"&&n!=2)arity_error("2");
            if((f=="controlledgate"||f=="adjointgate")&&n>0&&e->args[0]->kind!=Expr::Kind::Str)throw std::runtime_error("line "+std::to_string(e->line)+": gate modifier first argument must be a string literal gate name");
            if(f=="powergate"&&n>0&&e->args[0]->kind!=Expr::Kind::Str)throw std::runtime_error("line "+std::to_string(e->line)+": powergate first argument must be a string literal gate name");
            if(f=="expectation_pauli"&&n==2&&e->args[1]->kind!=Expr::Kind::Str)throw std::runtime_error("line "+std::to_string(e->line)+": expectation_pauli basis must be a string literal");
            if(f=="measure_basis"&&n==2&&e->args[1]->kind!=Expr::Kind::Str)throw std::runtime_error("line "+std::to_string(e->line)+": measure_basis basis must be a string literal");
            if((f=="measure_basis"||f=="expectation_x"||f=="expectation_y"||f=="expectation_pauli"||f=="statevector"||f=="density_matrix"||f=="reduced_density"||f=="purity"||f=="fidelity")&&n>=1&&!is_quantum_reference_expr(e->args[0]))throw std::runtime_error("line "+std::to_string(e->line)+": "+f+" requires a qubit variable or indexed register reference");
            if(f=="fidelity"&&n==2&&!is_quantum_reference_expr(e->args[1]))throw std::runtime_error("line "+std::to_string(e->line)+": fidelity second argument must be a qubit reference");
            if(f=="controlledgate"&&n>=3&&!is_quantum_reference_expr(e->args[1]) )throw std::runtime_error("line "+std::to_string(e->line)+": controlledgate control must be a qubit reference");
            if(f=="controlledgate"&&n>=3&&!is_quantum_reference_expr(e->args[2]) )throw std::runtime_error("line "+std::to_string(e->line)+": controlledgate target must be a qubit reference");
            if(f=="mcxgate"&&n==2&&!is_quantum_reference_expr(e->args[1]))throw std::runtime_error("line "+std::to_string(e->line)+": mcxgate target must be a qubit reference");
            if(f=="mcphasegate"&&n==3&&!is_quantum_reference_expr(e->args[1]))throw std::runtime_error("line "+std::to_string(e->line)+": mcphasegate target must be a qubit reference");
            if(f=="mczgate"&&n==2&&!is_quantum_reference_expr(e->args[1]))throw std::runtime_error("line "+std::to_string(e->line)+": mczgate target must be a qubit reference");
            auto checkModifierSignature=[&](const std::string& gate,std::size_t actual,const std::string& modifier){
                const bool angle=(gate=="phasegate"||gate=="rxgate"||gate=="rygate"||gate=="rzgate");
                const bool single=(gate=="hgate"||gate=="xgate"||gate=="ygate"||gate=="zgate"||gate=="sgate"||gate=="tgate"||angle);
                if(modifier=="controlledgate") {if(!single)throw std::runtime_error("line "+std::to_string(e->line)+": controlledgate supports single-qubit gate names only");std::size_t want=angle?4:3;if(actual!=want)throw std::runtime_error("line "+std::to_string(e->line)+": controlledgate "+gate+" expects "+std::to_string(want)+" argument(s) including its gate-name");}
                else if(modifier=="powergate"){if(!single)throw std::runtime_error("line "+std::to_string(e->line)+": powergate supports single-qubit gate names only");std::size_t want=angle?4:3;if(actual!=want)throw std::runtime_error("line "+std::to_string(e->line)+": powergate "+gate+" expects "+std::to_string(want)+" argument(s) including its gate-name");}
                else {std::size_t want=single?(angle?3:2):(gate=="cxgate"||gate=="czgate"||gate=="swapgate"?3:(gate=="cphasegate"||gate=="ccxgate"||gate=="toffoligate"?4:0));if(want==0)throw std::runtime_error("line "+std::to_string(e->line)+": adjointgate does not support gate '"+gate+"'");if(actual!=want)throw std::runtime_error("line "+std::to_string(e->line)+": adjointgate "+gate+" expects "+std::to_string(want)+" argument(s) including its gate-name");}
            };
            if((f=="controlledgate"||f=="adjointgate"||f=="powergate")&&n>0&&e->args[0]->kind==Expr::Kind::Str)checkModifierSignature(e->args[0]->text,n,f);

            if(f=="powergate"&&n>=2&&!is_quantum_reference_expr(e->args[1]))throw std::runtime_error("line "+std::to_string(e->line)+": powergate requires a qubit reference");
            if((f=="controlledgate"&&n>=3)||(f=="adjointgate"&&n>=3)) {std::size_t qstart=1;std::size_t qend=n; if(f=="controlledgate")qend=3;else if(n==3&&e->args[0]->kind==Expr::Kind::Str&&(e->args[0]->text=="cxgate"||e->args[0]->text=="czgate"||e->args[0]->text=="swapgate"))qend=3;else if(n==4&&e->args[0]->kind==Expr::Kind::Str&&e->args[0]->text=="cphasegate")qend=3;else if(n==4&&e->args[0]->kind==Expr::Kind::Str&&(e->args[0]->text=="ccxgate"||e->args[0]->text=="toffoligate"))qend=n;else qend=2;for(std::size_t k=qstart;k<qend;++k)if(!is_quantum_reference_expr(e->args[k]))throw std::runtime_error("line "+std::to_string(e->line)+": gate modifier operands must be qubit references");}

            if((f=="ccxgate"||f=="toffoligate")&&n!=3)arity_error("3");
            if((f=="print"||f=="yaz"||f=="type"||f=="abs"||f=="sqrt"||f=="sin"||f=="cos"||f=="tan"||f=="asin"||f=="acos"||f=="atan"||f=="exp"||f=="log"||f=="log10"||f=="floor"||f=="ceil"||f=="round"||f=="partner"||f=="zeryafonk"||f=="norm"||f=="assert_normalized"||f=="measure"||f=="len"||f=="to_number"||f=="to_string")&&n!=1)arity_error("1");
            if((f=="pow"||f=="probability"||f=="same_register"||f=="get")&&n!=2)arity_error("2");
            if(f=="set"&&n!=3) arity_error("3");
            if(f=="push"&&n!=2) arity_error("2");
            if(f=="pop"&&n!=1) arity_error("1");
            if((f=="superposfonk"||f=="moduleradim"||f=="input")&&n>1) arity_error("0 or 1");
            if(f=="evrimfonk"&&(n<1||n>2)) arity_error("1 or 2");
            if(f=="verify_entanglement"&&(n<1||n>2)) arity_error("1 or 2");
            if(f=="donfonk"&&n!=0&&n!=2) arity_error("0 or 2");
            if((f=="read_file"||f=="file_exists"||f=="upper"||f=="lower")&&n!=1) arity_error("1");
            if((f=="write_file"||f=="contains"||f=="split"||f=="join")&&n!=2) arity_error("2");
            if(f=="substr"&&(n<2||n>3)) arity_error("2 or 3");
            if(f=="assert"&&(n<1||n>2)) arity_error("1 or 2");
            if((f=="hgate"||f=="xgate"||f=="zgate")&&n!=1) arity_error("1");
            if((f=="phasegate"||f=="rxgate"||f=="rygate"||f=="rzgate")&&n!=2) arity_error("2");
            if((f=="cxgate"||f=="czgate"||f=="swapgate")&&n!=2) arity_error("2");
            if(f=="teleportfonk"&&n==2)for(const auto& a:e->args)if(a->kind!=Expr::Kind::Var)throw std::runtime_error("line "+std::to_string(e->line)+": teleportfonk currently requires direct DRNCQ variable names because it rebinds the destination");
            if((f=="donfonk"||f=="cxgate"||f=="czgate"||f=="swapgate")&&n==2)for(const auto& a:e->args)if(!is_quantum_reference_expr(a))throw std::runtime_error("line "+std::to_string(e->line)+": "+f+" requires qubit variables or indexed register references");
            if((f=="cphasegate")&&n==3)for(std::size_t k=0;k<2;++k)if(!is_quantum_reference_expr(e->args[k]))throw std::runtime_error("line "+std::to_string(e->line)+": cphasegate requires qubit variables or indexed register references");
            if((f=="bloch_vector"||f=="entanglement_entropy"||f=="sample_counts"||f=="bit_flip_noise"||f=="phase_flip_noise"||f=="depolarizing_noise"||f=="phase_damping_noise"||f=="amplitude_damping")&&n>=1&&!is_quantum_reference_expr(e->args[0]))throw std::runtime_error("line "+std::to_string(e->line)+": "+f+" requires a qubit variable or indexed register reference");
            if((f=="ccxgate"||f=="toffoligate")&&n==3)for(std::size_t k=0;k<3;++k)if(!is_quantum_reference_expr(e->args[k]))throw std::runtime_error("line "+std::to_string(e->line)+": Toffoli requires qubit variables or indexed register references");
            if(f=="__assign"){if(n!=2||e->args[0]->kind!=Expr::Kind::Str)throw std::runtime_error("internal for-loop assignment malformed");if(!name_known(e->args[0]->text,scopes))throw std::runtime_error("line "+std::to_string(e->line)+": for-loop assigns undeclared variable '"+e->args[0]->text+"'");}
            if((f=="min"||f=="max")&&n<1)arity_error("at least 1");
        }
        if(e->kind==Expr::Kind::List){if(e->args.size()>100000)throw std::runtime_error("list literal exceeds limit");}
        validate_expr(e->left,scopes,inFunction);validate_expr(e->right,scopes,inFunction);
        for(std::size_t k=0;k<e->args.size();++k){
            bool pureOut=e->kind==Expr::Kind::Call&&functions_.count(e->text)&&k<functions_.at(e->text).modes.size()&&functions_.at(e->text).modes[k]==1;
            if(!pureOut) validate_expr(e->args[k],scopes,inFunction);
        }
    }
    void validate_stmt(const S& s,std::vector<std::set<std::string>>& scopes,bool inFunction,int loopDepth){
        switch(s->kind){
        case Stmt::Kind::Decl:if(keyword_or_builtin(s->name)||functions_.count(s->name)||classes_.count(s->name))throw std::runtime_error("line "+std::to_string(s->line)+": variable name '"+s->name+"' is reserved");validate_expr(s->expr,scopes,inFunction);if(scopes.empty()){if(!globals_.count(s->name))throw std::runtime_error("internal global declaration mismatch");}else if(!scopes.back().insert(s->name).second)throw std::runtime_error("line "+std::to_string(s->line)+": duplicate declaration '"+s->name+"' in same scope");break;
        case Stmt::Kind::Assign:if(!name_known(s->name,scopes))throw std::runtime_error("line "+std::to_string(s->line)+": assignment to undeclared variable '"+s->name+"'");validate_expr(s->expr,scopes,inFunction);break;
        case Stmt::Kind::IndexAssign:if(!name_known(s->name,scopes))throw std::runtime_error("line "+std::to_string(s->line)+": assignment to undeclared list '"+s->name+"'");validate_expr(s->indexExpr,scopes,inFunction);validate_expr(s->expr,scopes,inFunction);break;
        case Stmt::Kind::MemberAssign:validate_expr(s->objectExpr,scopes,inFunction);validate_expr(s->expr,scopes,inFunction);break;
        case Stmt::Kind::Expr:validate_expr(s->expr,scopes,inFunction);break;
        case Stmt::Kind::Block:scopes.emplace_back();for(const auto& q:s->children)validate_stmt(q,scopes,inFunction,loopDepth);scopes.pop_back();break;
        case Stmt::Kind::If:validate_expr(s->cond,scopes,inFunction);validate_stmt(s->thenStmt,scopes,inFunction,loopDepth);if(s->elseStmt)validate_stmt(s->elseStmt,scopes,inFunction,loopDepth);break;
        case Stmt::Kind::While:validate_expr(s->cond,scopes,inFunction);validate_stmt(s->body,scopes,inFunction,loopDepth+1);break;
        case Stmt::Kind::For:validate_stmt(s->init,scopes,inFunction,loopDepth);validate_expr(s->cond,scopes,inFunction);validate_expr(s->inc,scopes,inFunction);validate_stmt(s->body,scopes,inFunction,loopDepth+1);break;
        case Stmt::Kind::Return:if(!inFunction)throw std::runtime_error("line "+std::to_string(s->line)+": return is only legal inside a function");validate_expr(s->expr,scopes,inFunction);break;
        case Stmt::Kind::Break:case Stmt::Kind::Continue:if(loopDepth==0)throw std::runtime_error("line "+std::to_string(s->line)+": break/continue is only legal inside a loop");break;
        case Stmt::Kind::TryCatch:validate_stmt(s->body,scopes,inFunction,loopDepth);if(keyword_or_builtin(s->catchName))throw std::runtime_error("line "+std::to_string(s->line)+": catch variable name '"+s->catchName+"' is reserved");scopes.emplace_back();scopes.back().insert(s->catchName);validate_stmt(s->catchBody,scopes,inFunction,loopDepth);scopes.pop_back();break;
        case Stmt::Kind::Throw:validate_expr(s->expr,scopes,inFunction);break;
        }
    }
    std::size_t emit(std::vector<BI>& c,BO op,int line,std::string text={},double num=0,std::uint32_t a=0,std::vector<std::string> refs={}){BI i;i.op=op;i.line=line;i.text=std::move(text);i.num=num;i.a=a;i.refs=std::move(refs);c.push_back(std::move(i));return c.size()-1;}
    void patch(std::vector<BI>& c,std::size_t i,std::size_t target){if(target>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("jump target overflow");c.at(i).a=static_cast<std::uint32_t>(target);}
    void emit_expr(const E& e,std::vector<BI>& c){if(!e)throw std::runtime_error("internal compiler: null expression");switch(e->kind){
        case Expr::Kind::Num:emit(c,BO::PushNum,e->line,{},e->num);break;case Expr::Kind::Str:emit(c,BO::PushStr,e->line,e->text);break;case Expr::Kind::Bool:emit(c,BO::PushBool,e->line,{},0,e->boolean?1u:0u);break;case Expr::Kind::Null:emit(c,BO::PushNull,e->line);break;case Expr::Kind::Var:emit(c,BO::Load,e->line,e->text);break;
        case Expr::Kind::List:for(const auto& a:e->args)emit_expr(a,c);emit(c,BO::MakeList,e->line,{},0,static_cast<std::uint32_t>(e->args.size()));break;
        case Expr::Kind::Index:emit_expr(e->left,c);emit_expr(e->right,c);emit(c,BO::IndexGet,e->line);break;case Expr::Kind::Member:emit_expr(e->left,c);emit(c,BO::MemberGet,e->line,e->text);break;case Expr::Kind::MethodCall:emit_expr(e->left,c);for(auto& a:e->args)emit_expr(a,c);emit(c,BO::CallMethod,e->line,e->text,0,static_cast<std::uint32_t>(e->args.size()));break;case Expr::Kind::NewObj:for(auto& a:e->args)emit_expr(a,c);emit(c,BO::CallClass,e->line,e->text,0,static_cast<std::uint32_t>(e->args.size()));break;
        case Expr::Kind::Unary:emit_expr(e->left,c);if(e->text=="+")emit(c,BO::Pos,e->line);else if(e->text=="-")emit(c,BO::Neg,e->line);else if(e->text=="!")emit(c,BO::Not,e->line);break;
        case Expr::Kind::Binary:{if(e->text=="&&"){emit_expr(e->left,c);auto jf=emit(c,BO::JumpFalse,e->line,{},0,0);emit_expr(e->right,c);emit(c,BO::ToBool,e->line);auto j=emit(c,BO::Jump,e->line,{},0,0);patch(c,jf,c.size());emit(c,BO::PushBool,e->line,{},0,0);patch(c,j,c.size());break;}if(e->text=="||"){emit_expr(e->left,c);auto jf=emit(c,BO::JumpFalse,e->line,{},0,0);emit(c,BO::PushBool,e->line,{},0,1);auto j=emit(c,BO::Jump,e->line,{},0,0);patch(c,jf,c.size());emit_expr(e->right,c);emit(c,BO::ToBool,e->line);patch(c,j,c.size());break;}emit_expr(e->left,c);emit_expr(e->right,c);BO op;if(e->text=="+")op=BO::Add;else if(e->text=="-")op=BO::Sub;else if(e->text=="*")op=BO::Mul;else if(e->text=="/")op=BO::Div;else if(e->text=="%")op=BO::Mod;else if(e->text=="^")op=BO::Pow;else if(e->text=="==")op=BO::Eq;else if(e->text=="!=")op=BO::Ne;else if(e->text=="<")op=BO::Lt;else if(e->text=="<=")op=BO::Le;else if(e->text==">")op=BO::Gt;else if(e->text==">=")op=BO::Ge;else throw std::runtime_error("internal compiler: unknown operator");emit(c,op,e->line,e->text);break;}
        case Expr::Kind::Call:{std::vector<std::string> refs;for(std::size_t k=0;k<e->args.size();++k){const auto& a=e->args[k];refs.push_back(ref_descriptor(a));bool pureOut=functions_.count(e->text)&&k<functions_.at(e->text).modes.size()&&functions_.at(e->text).modes[k]==1;if(pureOut)emit(c,BO::PushNull,a->line);else emit_expr(a,c);}emit(c,classes_.count(e->text)?BO::CallClass:(functions_.count(e->text)?BO::CallUser:BO::Call),e->line,e->text,0,static_cast<std::uint32_t>(e->args.size()),std::move(refs));break;}
    }}
    void emit_stmt(const S& s,std::vector<BI>& c,int depth,bool inFunction){if(depth>512)throw std::runtime_error("nested control-flow limit exceeded");switch(s->kind){
        case Stmt::Kind::Decl:emit_expr(s->expr,c);emit(c,BO::Declare,s->line,s->name);break;
        case Stmt::Kind::Assign:emit_expr(s->expr,c);emit(c,BO::Store,s->line,s->name);break;
        case Stmt::Kind::IndexAssign:emit(c,BO::Load,s->line,s->name);emit_expr(s->indexExpr,c);emit_expr(s->expr,c);emit(c,BO::IndexSet,s->line,s->name);break;
        case Stmt::Kind::MemberAssign:emit_expr(s->objectExpr,c);emit_expr(s->expr,c);emit(c,BO::MemberSet,s->line,s->memberName);break;
        case Stmt::Kind::Expr:emit_expr(s->expr,c);emit(c,BO::Pop,s->line);break;
        case Stmt::Kind::Block:emit(c,BO::EnterScope,s->line);++scopeDepth_;for(auto& q:s->children)emit_stmt(q,c,depth+1,inFunction);--scopeDepth_;emit(c,BO::ExitScope,s->line);break;
        case Stmt::Kind::If:{emit_expr(s->cond,c);auto jf=emit(c,BO::JumpFalse,s->line,{},0,0);emit_stmt(s->thenStmt,c,depth+1,inFunction);if(s->elseStmt){auto j=emit(c,BO::Jump,s->line,{},0,0);patch(c,jf,c.size());emit_stmt(s->elseStmt,c,depth+1,inFunction);patch(c,j,c.size());}else patch(c,jf,c.size());break;}
        case Stmt::Kind::While:{std::size_t top=c.size();emit_expr(s->cond,c);auto jf=emit(c,BO::JumpFalse,s->line,{},0,0);loop_stack_.push_back({{}, {}, scopeDepth_});emit_stmt(s->body,c,depth+1,inFunction);std::size_t cont=c.size();for(auto j:loop_stack_.back().continues)patch(c,j,top);emit(c,BO::Jump,s->line,{},0,static_cast<std::uint32_t>(top));std::size_t end=c.size();patch(c,jf,end);for(auto j:loop_stack_.back().breaks)patch(c,j,end);loop_stack_.pop_back();(void)cont;break;}
        case Stmt::Kind::For:{emit_stmt(s->init,c,depth+1,inFunction);std::size_t top=c.size();emit_expr(s->cond,c);auto jf=emit(c,BO::JumpFalse,s->line,{},0,0);loop_stack_.push_back({{}, {}, scopeDepth_});emit_stmt(s->body,c,depth+1,inFunction);std::size_t cont=c.size();for(auto j:loop_stack_.back().continues)patch(c,j,cont);emit_expr(s->inc,c);emit(c,BO::Pop,s->line);emit(c,BO::Jump,s->line,{},0,static_cast<std::uint32_t>(top));std::size_t end=c.size();patch(c,jf,end);for(auto j:loop_stack_.back().breaks)patch(c,j,end);loop_stack_.pop_back();break;}
        case Stmt::Kind::Return:if(s->expr)emit_expr(s->expr,c);else emit(c,BO::PushNull,s->line);emit(c,BO::Return,s->line,{},0,s->expr?1u:0u);break;
        case Stmt::Kind::Break:if(loop_stack_.empty())throw std::runtime_error("internal: break outside loop");for(int d=scopeDepth_;d>loop_stack_.back().scopeDepth;--d)emit(c,BO::ExitScope,s->line);loop_stack_.back().breaks.push_back(emit(c,BO::Jump,s->line,{},0,0));break;
        case Stmt::Kind::Continue:if(loop_stack_.empty())throw std::runtime_error("internal: continue outside loop");for(int d=scopeDepth_;d>loop_stack_.back().scopeDepth;--d)emit(c,BO::ExitScope,s->line);loop_stack_.back().continues.push_back(emit(c,BO::Jump,s->line,{},0,0));break;
        case Stmt::Kind::TryCatch:{auto tb=emit(c,BO::TryBegin,s->line,s->catchName,0,0);emit_stmt(s->body,c,depth+1,inFunction);emit(c,BO::TryEnd,s->line);auto j=emit(c,BO::Jump,s->line,{},0,0);patch(c,tb,c.size());emit_stmt(s->catchBody,c,depth+1,inFunction);emit(c,BO::ExitScope,s->line);patch(c,j,c.size());break;}
        case Stmt::Kind::Throw:emit_expr(s->expr,c);emit(c,BO::Throw,s->line);break;
    }}
};

// -------------------------- Values and quantum VM --------------------------
struct QSector {double evet_energy{1.0}; double hayir_energy{SQRT2}; std::string evet_label{"EVET"}; std::string hayir_label{"HAYIR"};};
struct QReg {int n{1};std::vector<C> amp;std::vector<QSector> sectors;std::uint64_t serial{0};};
struct QRef {std::shared_ptr<QReg> reg;int index{0};};
struct Arr; struct Obj; using V=std::variant<std::monostate,double,std::string,bool,QRef,std::shared_ptr<Arr>,std::shared_ptr<Obj>>;
struct Arr { std::vector<V> values; };
struct Obj { std::string className; std::map<std::string,V> fields; };
static bool is_num(const V& v){return std::holds_alternative<double>(v);}
static std::string type_name(const V& v){if(std::holds_alternative<std::monostate>(v))return "null";if(is_num(v))return "number";if(std::holds_alternative<std::string>(v))return "string";if(std::holds_alternative<bool>(v))return "bool";if(std::holds_alternative<QRef>(v))return "DRNCQ";if(std::holds_alternative<std::shared_ptr<Arr>>(v))return "list";return std::get<std::shared_ptr<Obj>>(v)->className;}
static std::string value_string_impl(const V& v,std::set<const Arr*>& active,unsigned depth){
    if(depth>64)return "<depth-limit>";
    if(std::holds_alternative<std::monostate>(v))return "null";
    if(auto p=std::get_if<double>(&v)){std::ostringstream o;o<<std::setprecision(12)<<*p;return o.str();}
    if(auto p=std::get_if<std::string>(&v))return *p;
    if(auto p=std::get_if<bool>(&v))return *p?"true":"false";
    if(auto q=std::get_if<QRef>(&v))return "DRNCQ<qubit "+std::to_string(q->index)+", registers="+std::to_string(q->reg->n)+">";
    if(auto o=std::get_if<std::shared_ptr<Obj>>(&v))return "<"+(*o)->className+" object>";
    auto a=std::get<std::shared_ptr<Arr>>(v);if(!a)return "null-list";if(active.count(a.get()))return "[<cycle>]";active.insert(a.get());std::string out="[";for(std::size_t i=0;i<a->values.size();++i){if(i)out+=", ";out+=value_string_impl(a->values[i],active,depth+1);}active.erase(a.get());return out+"]";
}
static std::string value_string(const V& v){std::set<const Arr*> active;return value_string_impl(v,active,0);}
static double as_num(const V& v,const std::string& context){if(auto p=std::get_if<double>(&v))return *p; if(auto p=std::get_if<bool>(&v))return *p?1.0:0.0;throw std::runtime_error(context+" requires a numeric value, got "+type_name(v));}
static bool truth(const V& v){if(std::holds_alternative<std::monostate>(v))return false;if(auto p=std::get_if<bool>(&v))return *p;if(auto p=std::get_if<double>(&v))return *p!=0.0;if(auto p=std::get_if<std::string>(&v))return !p->empty();if(std::holds_alternative<std::shared_ptr<Arr>>(v)||std::holds_alternative<std::shared_ptr<Obj>>(v))return true;throw std::runtime_error("DRNCQ quantum state cannot be used as a condition; measure or inspect it explicitly");}
static void require_finite(double x,const char* op){if(!std::isfinite(x))throw std::runtime_error(std::string("non-finite result in ")+op);}

class VM {
public:
    VM():rng_(std::random_device{}()){}
    ~VM(){for(auto& w:arr_registry_)if(auto a=w.lock())a->values.clear();for(auto& w:obj_registry_)if(auto o=w.lock())o->fields.clear();}
    void execute(const ByteProgram& p){program_=p;pc_=0;stack_.clear();scopes_.clear();calls_.clear();handlers_.clear();std::uint64_t count=0;while(pc_<program_.code.size()){if(++count>MAX_VM_STEPS)throw std::runtime_error("VM instruction budget exceeded (possible runaway loop)");const BI i=program_.code[pc_++];try{step(i);}catch(const std::exception& e){if(!handlers_.empty()){auto h=handlers_.back();handlers_.pop_back();if(stack_.size()>h.stackDepth)stack_.resize(h.stackDepth);if(scopes_.size()>h.scopeDepth)scopes_.resize(h.scopeDepth);if(calls_.size()>h.callDepth)calls_.resize(h.callDepth);scopes_.push_back({{h.name,std::string(e.what())}});pc_=h.target;continue;}throw std::runtime_error("line "+std::to_string(i.line)+" ["+opcode_name(i.op)+"]: "+e.what());}if(i.op==BO::Halt)break;}}
    static std::string opcode_name(BO op){switch(op){case BO::PushNum:return"PUSH_NUM";case BO::PushStr:return"PUSH_STR";case BO::PushBool:return"PUSH_BOOL";case BO::PushNull:return"PUSH_NULL";case BO::Load:return"LOAD";case BO::Declare:return"DECLARE";case BO::Store:return"STORE";case BO::Pop:return"POP";case BO::Add:return"ADD";case BO::Sub:return"SUB";case BO::Mul:return"MUL";case BO::Div:return"DIV";case BO::Mod:return"MOD";case BO::Pow:return"POW";case BO::Pos:return"POS";case BO::Neg:return"NEG";case BO::Not:return"NOT";case BO::Eq:return"EQ";case BO::Ne:return"NE";case BO::Lt:return"LT";case BO::Le:return"LE";case BO::Gt:return"GT";case BO::Ge:return"GE";case BO::ToBool:return"TO_BOOL";case BO::Call:return"CALL_BUILTIN";case BO::CallUser:return"CALL_USER";case BO::Jump:return"JUMP";case BO::JumpFalse:return"JUMP_FALSE";case BO::Halt:return"HALT";case BO::MakeList:return"MAKE_LIST";case BO::IndexGet:return"INDEX_GET";case BO::IndexSet:return"INDEX_SET";case BO::Return:return"RETURN";case BO::EnterScope:return"ENTER_SCOPE";case BO::ExitScope:return"EXIT_SCOPE";case BO::TryBegin:return"TRY_BEGIN";case BO::TryEnd:return"TRY_END";case BO::Throw:return"THROW";case BO::MemberGet:return"MEMBER_GET";case BO::MemberSet:return"MEMBER_SET";case BO::CallMethod:return"CALL_METHOD";case BO::CallClass:return"CALL_CLASS";}return"?";}
    static void print_about(){std::cout<<"DRNC-Q v1.8 Full Edition standalone native compiler/DRNQB7 VM; PC/QPC targets\nCore: fonk functions with input/output parameters and return values, recursion, classes/methods, modules, scopes, lists, strings, file I/O, exceptions, if/else, for/while, break/continue\nQuantum: auto-generated EVET/HAYIR sectors, 1..16 qubit registers, H/X/Y/Z/S/T, rotations, generic controlled/adjoint/power gate modifiers, multi-controlled X/phase gates, X/Y/Z basis measurement, density-matrix diagnostics, state vectors, fidelity, shot counts, Bloch observables, five stochastic noise trajectories, Zerya branches and teleport simulation\nModular s is an internal dimensionless relational parameter, not seconds\nExecution: built-in state-vector simulator; no provider SDK/API required\nPhysical QPU: needs independent compatible DRNC-QP1 control electronics/firmware; no physical hardware is bundled\n";}
private:
    struct CallFrame {std::size_t returnPc,stackDepth,scopeDepth,handlerDepth;std::optional<V> forcedReturn;std::vector<std::pair<std::string,std::string>> outputBindings{};};
    struct Handler {std::size_t target,stackDepth,scopeDepth,callDepth;std::string name;};
    ByteProgram program_;std::size_t pc_{0};std::vector<V> stack_;std::map<std::string,V> env_;std::vector<std::map<std::string,V>> scopes_;std::vector<CallFrame> calls_;std::vector<Handler> handlers_;std::vector<std::weak_ptr<QReg>> regs_;std::vector<std::weak_ptr<Arr>> arr_registry_;std::vector<std::weak_ptr<Obj>> obj_registry_;std::uint64_t next_reg_{1},mod_steps_{0};double modular_s_{0};std::mt19937_64 rng_;
    V* find_value(const std::string& n){for(auto it=scopes_.rbegin();it!=scopes_.rend();++it){auto f=it->find(n);if(f!=it->end())return &f->second;}auto g=env_.find(n);return g==env_.end()?nullptr:&g->second;}
    const V* find_value(const std::string& n)const{for(auto it=scopes_.rbegin();it!=scopes_.rend();++it){auto f=it->find(n);if(f!=it->end())return &f->second;}auto g=env_.find(n);return g==env_.end()?nullptr:&g->second;}
    V get_value(const std::string& n){auto p=find_value(n);if(!p)throw std::runtime_error("undeclared or not-yet-initialized variable '"+n+"'");return *p;}
    void set_value(const std::string& n,V v){for(auto it=scopes_.rbegin();it!=scopes_.rend();++it){auto f=it->find(n);if(f!=it->end()){f->second=std::move(v);return;}}auto g=env_.find(n);if(g==env_.end())throw std::runtime_error("assignment to undeclared variable '"+n+"'");g->second=std::move(v);}
    static bool indexed_ref(const std::string& ref,std::string& name,std::size_t& index){auto marker=ref.find("[#");if(marker==std::string::npos||ref.empty()||ref.back()!=']')return false;name=ref.substr(0,marker);std::string digits=ref.substr(marker+2,ref.size()-marker-3);if(name.empty()||digits.empty())return false;std::uint64_t x=0;for(char c:digits){if(!std::isdigit(static_cast<unsigned char>(c)))return false;if(x>10000000)throw std::runtime_error("indexed quantum reference out of range");x=x*10+static_cast<unsigned>(c-'0');}index=static_cast<std::size_t>(x);return true;}
    V get_ref_value(const std::string& ref){std::string name;std::size_t ix=0;if(!indexed_ref(ref,name,ix))return get_value(ref);auto arr=as_array(get_value(name),"indexed quantum reference");if(ix>=arr->values.size())throw std::runtime_error("indexed quantum reference out of bounds: "+ref);return arr->values[ix];}
    void set_ref_value(const std::string& ref,V value){std::string name;std::size_t ix=0;if(!indexed_ref(ref,name,ix)){set_value(ref,std::move(value));return;}auto arr=as_array(get_value(name),"indexed quantum reference");if(ix>=arr->values.size())throw std::runtime_error("indexed quantum reference out of bounds: "+ref);if(would_create_cycle(value,arr.get(),nullptr))throw std::runtime_error("container reference cycle is not permitted");arr->values[ix]=std::move(value);}
    void declare_value(const std::string& n,V v){if(scopes_.empty()){if(env_.count(n))throw std::runtime_error("variable '"+n+"' already declared");env_.emplace(n,std::move(v));}else{if(scopes_.back().count(n))throw std::runtime_error("variable '"+n+"' already declared in this scope");scopes_.back().emplace(n,std::move(v));}}
    std::size_t as_index(const V& v,const std::string& ctx){double x=as_num(v,ctx);if(x<0||std::floor(x)!=x||x>10000000)throw std::runtime_error(ctx+" requires a non-negative integer index");return static_cast<std::size_t>(x);}
    std::shared_ptr<Arr> as_array(const V& v,const std::string& ctx){auto p=std::get_if<std::shared_ptr<Arr>>(&v);if(!p||!*p)throw std::runtime_error(ctx+" requires a list value");return *p;}
    std::shared_ptr<Obj> as_object(const V& v,const std::string& ctx){auto p=std::get_if<std::shared_ptr<Obj>>(&v);if(!p||!*p)throw std::runtime_error(ctx+" requires an object instance");return *p;}
    std::shared_ptr<Arr> new_array(){auto a=std::make_shared<Arr>();if(arr_registry_.size()>100000)arr_registry_.erase(std::remove_if(arr_registry_.begin(),arr_registry_.end(),[](const auto& w){return w.expired();}),arr_registry_.end());arr_registry_.push_back(a);return a;}
    std::shared_ptr<Obj> new_object(const std::string& className){auto o=std::make_shared<Obj>();o->className=className;if(obj_registry_.size()>100000)obj_registry_.erase(std::remove_if(obj_registry_.begin(),obj_registry_.end(),[](const auto& w){return w.expired();}),obj_registry_.end());obj_registry_.push_back(o);return o;}
    bool reaches_container(const V& v,const Arr* targetArray,const Obj* targetObject,std::set<const Arr*>& seenArrays,std::set<const Obj*>& seenObjects){
        if(auto a=std::get_if<std::shared_ptr<Arr>>(&v)){if(!*a)return false;if(targetArray&&a->get()==targetArray)return true;if(!seenArrays.insert(a->get()).second)return false;for(const auto& item:(*a)->values)if(reaches_container(item,targetArray,targetObject,seenArrays,seenObjects))return true;return false;}
        if(auto o=std::get_if<std::shared_ptr<Obj>>(&v)){if(!*o)return false;if(targetObject&&o->get()==targetObject)return true;if(!seenObjects.insert(o->get()).second)return false;for(const auto& item:(*o)->fields)if(reaches_container(item.second,targetArray,targetObject,seenArrays,seenObjects))return true;}
        return false;
    }
    bool would_create_cycle(const V& v,const Arr* targetArray,const Obj* targetObject){std::set<const Arr*> seenArrays;std::set<const Obj*> seenObjects;return reaches_container(v,targetArray,targetObject,seenArrays,seenObjects);}
    V pop(){if(stack_.empty())throw std::runtime_error("stack underflow");V v=std::move(stack_.back());stack_.pop_back();return v;}
    void push(V v){if(stack_.size()>100000u)throw std::runtime_error("VM stack depth limit exceeded");stack_.push_back(std::move(v));}
    QRef qref(const V& v,const std::string& ctx){auto p=std::get_if<QRef>(&v);if(!p||!p->reg)throw std::runtime_error(ctx+" requires a DRNCQ quantum variable");return *p;}
    std::vector<QRef> control_refs(const V& v,const std::string& ctx){std::vector<QRef> out;if(auto q=std::get_if<QRef>(&v)){out.push_back(qref(*q,ctx));return out;}if(auto a=std::get_if<std::shared_ptr<Arr>>(&v)){if(!*a||(*a)->values.empty())throw std::runtime_error(ctx+" requires a non-empty list of control qubits");for(const auto& item:(*a)->values)out.push_back(qref(item,ctx));return out;}throw std::runtime_error(ctx+" first argument must be a qubit or a list of qubit references");}
    std::shared_ptr<QReg> new_reg(int n,const std::vector<C>& amps){if(n<1||n>MAX_QUBITS)throw std::runtime_error("qubit register limit is 1..16");if(amps.size()!=(std::size_t(1)<<n))throw std::runtime_error("internal state vector dimension mismatch");auto r=std::make_shared<QReg>();r->n=n;r->amp=amps;r->sectors.assign(static_cast<std::size_t>(n),QSector{});r->serial=next_reg_++;regs_.push_back(r);return r;}
    void normalize(QReg& r){double n=0;for(C a:r.amp)n+=std::norm(a);if(!std::isfinite(n)||n<EPS)throw std::runtime_error("quantum state has zero/non-finite norm");double f=1/std::sqrt(n);for(auto& a:r.amp)a*=f;}
    double norm2(const QReg& r)const{double x=0;for(C a:r.amp)x+=std::norm(a);return x;}
    void evolve(QReg& r,double ds){for(std::size_t basis=0;basis<r.amp.size();++basis){double e=0;for(int q=0;q<r.n;++q)e+=((basis>>q)&1)?r.sectors[static_cast<std::size_t>(q)].hayir_energy:r.sectors[static_cast<std::size_t>(q)].evet_energy;r.amp[basis]*=std::polar(1.0,-e*ds);}}
    void modular_advance(double ds=1.0,bool apply_spectral_evolution=false){if(!std::isfinite(ds)||std::abs(ds)>1e6)throw std::runtime_error("modular increment must be finite and |ds| <= 1e6");modular_s_+=ds;++mod_steps_;std::set<QReg*> seen;for(auto it=regs_.begin();it!=regs_.end();){if(auto r=it->lock()){if(apply_spectral_evolution&&seen.insert(r.get()).second)evolve(*r,ds);++it;}else it=regs_.erase(it);}}
    void h_gate(QReg& r,int q){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");std::size_t m=std::size_t(1)<<q;const double k=1/std::sqrt(2.0);for(std::size_t i=0;i<r.amp.size();++i)if(!(i&m)){C a=r.amp[i],b=r.amp[i|m];r.amp[i]=(a+b)*k;r.amp[i|m]=(a-b)*k;}}
    void x_gate(QReg& r,int q){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");std::size_t m=std::size_t(1)<<q;for(std::size_t i=0;i<r.amp.size();++i)if(!(i&m))std::swap(r.amp[i],r.amp[i|m]);}
    void z_gate(QReg& r,int q){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");std::size_t m=std::size_t(1)<<q;for(std::size_t i=0;i<r.amp.size();++i)if(i&m)r.amp[i]=-r.amp[i];}
    void y_gate(QReg& r,int q){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");std::size_t m=std::size_t(1)<<q;for(std::size_t i=0;i<r.amp.size();++i)if(!(i&m)){C a=r.amp[i],b=r.amp[i|m];r.amp[i]=C(0,-1)*b;r.amp[i|m]=C(0,1)*a;}}
    void s_gate(QReg& r,int q){phase_gate(r,q,PI/2.0);}
    void t_gate(QReg& r,int q){phase_gate(r,q,PI/4.0);}
    void cphase_gate(QReg& r,int a,int b,double theta){if(a<0||a>=r.n||b<0||b>=r.n||a==b)throw std::runtime_error("cphasegate requires two distinct in-range qubits");if(!std::isfinite(theta))throw std::runtime_error("controlled phase angle must be finite");const std::size_t ma=std::size_t(1)<<a,mb=std::size_t(1)<<b;C factor=std::polar(1.0,theta);for(std::size_t i=0;i<r.amp.size();++i)if((i&ma)&&(i&mb))r.amp[i]*=factor;}
    void toffoli_gate(QReg& r,int c1,int c2,int target){if(c1<0||c1>=r.n||c2<0||c2>=r.n||target<0||target>=r.n||c1==c2||c1==target||c2==target)throw std::runtime_error("Toffoli requires three distinct in-range qubits");const std::size_t m1=std::size_t(1)<<c1,m2=std::size_t(1)<<c2,mt=std::size_t(1)<<target;for(std::size_t i=0;i<r.amp.size();++i)if((i&m1)&&(i&m2)&&!(i&mt))std::swap(r.amp[i],r.amp[i|mt]);}
    void validate_multi_controls(const QReg& r,const std::vector<int>& controls,int target){
        if(target<0||target>=r.n)throw std::runtime_error("multi-controlled gate target index out of range");
        if(controls.empty())throw std::runtime_error("multi-controlled gate requires at least one control");
        std::set<int> seen;
        for(int c:controls){if(c<0||c>=r.n)throw std::runtime_error("multi-controlled gate control index out of range");if(c==target)throw std::runtime_error("multi-controlled gate target must not also be a control");if(!seen.insert(c).second)throw std::runtime_error("multi-controlled gate controls must be distinct");}
    }
    void mcx_gate(QReg& r,const std::vector<int>& controls,int target){
        validate_multi_controls(r,controls,target);std::size_t mask=0,tm=std::size_t(1)<<target;for(int c:controls)mask|=std::size_t(1)<<c;
        for(std::size_t i=0;i<r.amp.size();++i)if(!(i&tm)&&(i&mask)==mask)std::swap(r.amp[i],r.amp[i|tm]);
    }
    void mcphase_gate(QReg& r,const std::vector<int>& controls,int target,double theta){
        validate_multi_controls(r,controls,target);if(!std::isfinite(theta))throw std::runtime_error("multi-controlled phase angle must be finite");std::size_t mask=std::size_t(1)<<target;for(int c:controls)mask|=std::size_t(1)<<c;C factor=std::polar(1.0,theta);for(std::size_t i=0;i<r.amp.size();++i)if((i&mask)==mask)r.amp[i]*=factor;
    }
    void phase_gate(QReg& r,int q,double phase){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");if(!std::isfinite(phase))throw std::runtime_error("phase must be finite");std::size_t m=std::size_t(1)<<q;C factor=std::polar(1.0,phase);for(std::size_t i=0;i<r.amp.size();++i)if(i&m)r.amp[i]*=factor;}
    void rx_gate(QReg& r,int q,double theta){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");if(!std::isfinite(theta))throw std::runtime_error("rotation angle must be finite");std::size_t m=std::size_t(1)<<q;double c=std::cos(theta/2),sn=std::sin(theta/2);C minus_i(0,-sn);for(std::size_t i=0;i<r.amp.size();++i)if(!(i&m)){C a=r.amp[i],b=r.amp[i|m];r.amp[i]=c*a+minus_i*b;r.amp[i|m]=minus_i*a+c*b;}}
    void ry_gate(QReg& r,int q,double theta){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");if(!std::isfinite(theta))throw std::runtime_error("rotation angle must be finite");std::size_t m=std::size_t(1)<<q;double c=std::cos(theta/2),sn=std::sin(theta/2);for(std::size_t i=0;i<r.amp.size();++i)if(!(i&m)){C a=r.amp[i],b=r.amp[i|m];r.amp[i]=c*a-sn*b;r.amp[i|m]=sn*a+c*b;}}
    void rz_gate(QReg& r,int q,double theta){if(q<0||q>=r.n)throw std::runtime_error("qubit index out of range");if(!std::isfinite(theta))throw std::runtime_error("rotation angle must be finite");std::size_t m=std::size_t(1)<<q;C lo=std::polar(1.0,-theta/2),hi=std::polar(1.0,theta/2);for(std::size_t i=0;i<r.amp.size();++i)r.amp[i]*=(i&m)?hi:lo;}
    using U2 = std::array<C,4>; // Row-major 2x2 unitary.
    U2 single_matrix(const std::string& name,double theta=0.0,bool hasTheta=false,bool adjoint=false){
        U2 u{}; const C I(0,1); const double k=1/std::sqrt(2.0);
        if(name=="hgate"){if(hasTheta)throw std::runtime_error("hgate takes no angle");u={k,k,k,-k};}
        else if(name=="xgate"){if(hasTheta)throw std::runtime_error("xgate takes no angle");u={C(0),C(1),C(1),C(0)};}
        else if(name=="ygate"){if(hasTheta)throw std::runtime_error("ygate takes no angle");u={C(0),-I,I,C(0)};}
        else if(name=="zgate"){if(hasTheta)throw std::runtime_error("zgate takes no angle");u={C(1),C(0),C(0),C(-1)};}
        else if(name=="sgate"){if(hasTheta)throw std::runtime_error("sgate takes no angle");u={C(1),C(0),C(0),I};}
        else if(name=="tgate"){if(hasTheta)throw std::runtime_error("tgate takes no angle");u={C(1),C(0),C(0),std::polar(1.0,PI/4)};}
        else if(name=="phasegate"){if(!hasTheta)throw std::runtime_error("phasegate modifier requires an angle");u={C(1),C(0),C(0),std::polar(1.0,theta)};}
        else if(name=="rxgate"){if(!hasTheta)throw std::runtime_error("rxgate modifier requires an angle");double c=std::cos(theta/2),v=std::sin(theta/2);u={C(c),C(0,-v),C(0,-v),C(c)};}
        else if(name=="rygate"){if(!hasTheta)throw std::runtime_error("rygate modifier requires an angle");double c=std::cos(theta/2),v=std::sin(theta/2);u={C(c),C(-v),C(v),C(c)};}
        else if(name=="rzgate"){if(!hasTheta)throw std::runtime_error("rzgate modifier requires an angle");u={std::polar(1.0,-theta/2),C(0),C(0),std::polar(1.0,theta/2)};}
        else throw std::runtime_error("gate modifier supports single-qubit gates hgate/xgate/ygate/zgate/sgate/tgate/phasegate/rxgate/rygate/rzgate");
        if(adjoint)u={std::conj(u[0]),std::conj(u[2]),std::conj(u[1]),std::conj(u[3])};
        return u;
    }
    U2 multiply_u2(const U2& a,const U2& b){return {a[0]*b[0]+a[1]*b[2],a[0]*b[1]+a[1]*b[3],a[2]*b[0]+a[3]*b[2],a[2]*b[1]+a[3]*b[3]};}
    U2 power_u2(U2 base,int power){if(power< -32||power>32)throw std::runtime_error("powergate exponent must be an integer in [-32,32]");if(power<0){base={std::conj(base[0]),std::conj(base[2]),std::conj(base[1]),std::conj(base[3])};power=-power;}U2 result={C(1),C(0),C(0),C(1)};while(power>0){if(power&1)result=multiply_u2(result,base);base=multiply_u2(base,base);power>>=1;}return result;}
    void apply_u2(QReg& r,int target,const U2& u,int control=-1){
        if(target<0||target>=r.n)throw std::runtime_error("unitary target qubit index out of range");
        if(control>=r.n||control< -1||control==target)throw std::runtime_error("unitary control must be a distinct in-range qubit");
        const std::size_t tm=std::size_t(1)<<target, cm=control<0?0:(std::size_t(1)<<control);
        for(std::size_t i=0;i<r.amp.size();++i){if((i&tm)!=0)continue;if(control>=0&&(i&cm)==0)continue;const std::size_t j=i|tm;const C a=r.amp[i],b=r.amp[j];r.amp[i]=u[0]*a+u[1]*b;r.amp[j]=u[2]*a+u[3]*b;}
    }
    std::shared_ptr<Arr> complex_pair(C z){auto a=new_array();a->values.emplace_back(z.real());a->values.emplace_back(z.imag());return a;}
    std::shared_ptr<Arr> statevector_value(const QRef& q){auto a=new_array();a->values.reserve(q.reg->amp.size());for(C z:q.reg->amp)a->values.emplace_back(complex_pair(z));return a;}
    std::shared_ptr<Arr> density_matrix_value(const QRef& q){const auto& r=*q.reg;if(r.n>8)throw std::runtime_error("density_matrix is limited to 8 qubits (dense O(4^n) output)");const std::size_t d=r.amp.size();double n2=norm2(r);if(!std::isfinite(n2)||n2<EPS)throw std::runtime_error("density_matrix requires a nonzero finite state");auto rows=new_array();rows->values.reserve(d);for(std::size_t i=0;i<d;++i){auto row=new_array();row->values.reserve(d);for(std::size_t j=0;j<d;++j)row->values.emplace_back(complex_pair(r.amp[i]*std::conj(r.amp[j])/n2));rows->values.emplace_back(row);}return rows;}
    std::shared_ptr<Arr> reduced_density_value(const QRef& q){const auto& r=*q.reg;const std::size_t mask=std::size_t(1)<<q.index;double total=0,p0=0,p1=0;C rho01{};for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if(!(i&mask)){p0+=w;rho01+=r.amp[i]*std::conj(r.amp[i|mask]);}else p1+=w;}if(!std::isfinite(total)||total<EPS)throw std::runtime_error("reduced_density requires a nonzero finite state");auto row0=new_array();row0->values.emplace_back(complex_pair(C(p0/total,0)));row0->values.emplace_back(complex_pair(rho01/total));auto row1=new_array();row1->values.emplace_back(complex_pair(std::conj(rho01)/total));row1->values.emplace_back(complex_pair(C(p1/total,0)));auto out=new_array();out->values.emplace_back(row0);out->values.emplace_back(row1);return out;}
    double pure_state_fidelity(const QRef& a,const QRef& b){if(a.reg->n!=b.reg->n)throw std::runtime_error("fidelity requires registers with the same number of qubits");if(a.reg==b.reg)return 1.0;C overlap{};for(std::size_t i=0;i<a.reg->amp.size();++i)overlap+=std::conj(a.reg->amp[i])*b.reg->amp[i];double na=norm2(*a.reg),nb=norm2(*b.reg);if(na<EPS||nb<EPS)throw std::runtime_error("fidelity requires nonzero states");return std::clamp(std::norm(overlap)/(na*nb),0.0,1.0);}
    void cz_gate(QReg& r,int a,int b){if(a<0||b<0||a>=r.n||b>=r.n||a==b)throw std::runtime_error("invalid CZ qubit indices");std::size_t ma=std::size_t(1)<<a,mb=std::size_t(1)<<b;for(std::size_t i=0;i<r.amp.size();++i)if((i&ma)&&(i&mb))r.amp[i]=-r.amp[i];}
    void swap_gate(QReg& r,int a,int b){if(a<0||b<0||a>=r.n||b>=r.n||a==b)throw std::runtime_error("invalid SWAP qubit indices");std::size_t ma=std::size_t(1)<<a,mb=std::size_t(1)<<b;for(std::size_t i=0;i<r.amp.size();++i)if(!(i&ma)&&(i&mb)){std::size_t j=i|ma;j&=~mb;std::swap(r.amp[i],r.amp[j]);}}
    void cx_gate(QReg& r,int ctrl,int tgt){if(ctrl==tgt||ctrl<0||tgt<0||ctrl>=r.n||tgt>=r.n)throw std::runtime_error("invalid CX qubit indices");std::size_t c=std::size_t(1)<<ctrl,t=std::size_t(1)<<tgt;for(std::size_t i=0;i<r.amp.size();++i)if((i&c)&&!(i&t))std::swap(r.amp[i],r.amp[i|t]);}
    void rewrite_value(V& value,const std::shared_ptr<QReg>& a,const std::shared_ptr<QReg>& b,const std::shared_ptr<QReg>& merged,int offset_b,std::set<const Arr*>& seenArrays,std::set<const Obj*>& seenObjects){
        if(auto q=std::get_if<QRef>(&value)){if(q->reg==a){q->reg=merged;}else if(b&&q->reg==b){q->reg=merged;q->index+=offset_b;}return;}
        if(auto arr=std::get_if<std::shared_ptr<Arr>>(&value)){if(!*arr||!seenArrays.insert(arr->get()).second)return;for(auto& item:(*arr)->values)rewrite_value(item,a,b,merged,offset_b,seenArrays,seenObjects);return;}
        if(auto obj=std::get_if<std::shared_ptr<Obj>>(&value)){if(!*obj||!seenObjects.insert(obj->get()).second)return;for(auto& kv:(*obj)->fields)rewrite_value(kv.second,a,b,merged,offset_b,seenArrays,seenObjects);}
    }
    void rewrite_refs(const std::shared_ptr<QReg>& a,const std::shared_ptr<QReg>& b,const std::shared_ptr<QReg>& merged,int offset_b){std::set<const Arr*> seenArrays;std::set<const Obj*> seenObjects;auto visit=[&](auto& m){for(auto& kv:m)rewrite_value(kv.second,a,b,merged,offset_b,seenArrays,seenObjects);};visit(env_);for(auto& frame:scopes_)visit(frame);}
    QRef merge(const QRef& qa,const QRef& qb){if(qa.reg==qb.reg)return qa;auto a=qa.reg,b=qb.reg;if(a->n+b->n>MAX_QUBITS)throw std::runtime_error("merged entangled register exceeds 16 qubits");int an=a->n,bn=b->n;std::vector<C> amp(std::size_t(1)<<(an+bn));for(std::size_t ib=0;ib<b->amp.size();++ib)for(std::size_t ia=0;ia<a->amp.size();++ia)amp[ia|(ib<<an)]=a->amp[ia]*b->amp[ib];auto m=new_reg(an+bn,amp);m->sectors.clear();m->sectors.insert(m->sectors.end(),a->sectors.begin(),a->sectors.end());m->sectors.insert(m->sectors.end(),b->sectors.begin(),b->sectors.end());rewrite_refs(a,b,m,an);return {m,qa.index};}
    QRef append_zero(const QRef& q){auto old=q.reg;int n=old->n;if(n>=MAX_QUBITS)throw std::runtime_error("register qubit limit exceeded");std::vector<C> a(old->amp.size()*2,C{});std::copy(old->amp.begin(),old->amp.end(),a.begin());auto r=new_reg(n+1,a);r->sectors=old->sectors;r->sectors.push_back(QSector{});rewrite_refs(old,nullptr,r,0);return {r,n};}
    int measure_bit(QReg& r,int q){if(q<0||q>=r.n)throw std::runtime_error("measurement qubit index out of range");std::size_t mask=std::size_t(1)<<q;double total=0,p1=0;for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if(i&mask)p1+=w;}if(!std::isfinite(total)||total<EPS)throw std::runtime_error("cannot measure a zero-norm or invalid state");p1/=total;std::uniform_real_distribution<double>d(0.0,1.0);int out=d(rng_)<p1?1:0;double p=out?p1:1-p1;if(p<EPS){out=1-out;p=out?p1:1-p1;}double f=1/std::sqrt(p*total);for(std::size_t i=0;i<r.amp.size();++i){if(static_cast<int>((i&mask)!=0)!=out)r.amp[i]=0;else r.amp[i]*=f;}return out;}
    double expectation_z_value(const QRef& q){const auto& r=*q.reg;double p0=0.0,p1=0.0,total=0.0;for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if((i>>q.index)&1)p1+=w;else p0+=w;}if(total<EPS||!std::isfinite(total))throw std::runtime_error("expectation_z requires a nonzero finite state");return (p0-p1)/total;}
    double reduced_purity(const QRef& q){const auto& r=*q.reg;if(r.n<2)return 1.0;std::size_t mask=std::size_t(1)<<q.index;double r00=0,r11=0;C r01{};for(std::size_t i=0;i<r.amp.size();++i){if(!(i&mask)){r00+=std::norm(r.amp[i]);r01+=r.amp[i]*std::conj(r.amp[i|mask]);}else r11+=std::norm(r.amp[i]);}return r00*r00+r11*r11+2*std::norm(r01);}
    static double checked_probability(double p,const std::string& name){
        if(!std::isfinite(p)||p<0.0||p>1.0)throw std::runtime_error(name+" probability must be finite and in [0,1]");
        return p;
    }
    bool random_event(double p){std::uniform_real_distribution<double> d(0.0,1.0);return d(rng_)<p;}
    void amplitude_damping(QReg& r,int q,double gamma){
        if(q<0||q>=r.n)throw std::runtime_error("amplitude_damping qubit index out of range");
        if(!std::isfinite(gamma)||gamma<0.0||gamma>1.0)throw std::runtime_error("amplitude_damping gamma must be finite and in [0,1]");
        const std::size_t mask=std::size_t(1)<<q; double total=0.0,excited=0.0;
        for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if(i&mask)excited+=w;}
        if(!std::isfinite(total)||total<EPS)throw std::runtime_error("amplitude_damping requires a nonzero finite state");
        const double jumpP=gamma*excited/total; const bool jump=random_event(jumpP);
        if(jump){
            std::vector<C> out(r.amp.size(),C{}); const double scale=std::sqrt(gamma);
            for(std::size_t i=0;i<r.amp.size();++i)if(i&mask)out[i&~mask]=scale*r.amp[i];
            r.amp.swap(out);
        } else {
            const double scale=std::sqrt(1.0-gamma);
            for(std::size_t i=0;i<r.amp.size();++i)if(i&mask)r.amp[i]*=scale;
        }
        const double n2=norm2(r); if(!std::isfinite(n2)||n2<EPS)throw std::runtime_error("amplitude_damping produced an invalid trajectory");
        const double scale=1.0/std::sqrt(n2);for(auto& a:r.amp)a*=scale;
    }
    std::shared_ptr<Arr> sample_counts(const V& value,double rawShots){
        if(!std::isfinite(rawShots)||std::floor(rawShots)!=rawShots||rawShots<1||rawShots>1000000)throw std::runtime_error("sample_counts shots must be an integer in 1..1000000");
        std::shared_ptr<QReg> r;
        if(auto q=std::get_if<QRef>(&value))r=q->reg;
        else if(auto ap=std::get_if<std::shared_ptr<Arr>>(&value)){
            if(!*ap||(*ap)->values.empty())throw std::runtime_error("sample_counts requires a non-empty quantum register");
            r=qref((*ap)->values.front(),"sample_counts").reg;
            if((*ap)->values.size()!=static_cast<std::size_t>(r->n))throw std::runtime_error("sample_counts list must contain every qubit in the register");
            std::set<int> seen;for(const auto& v:(*ap)->values){auto qr=qref(v,"sample_counts");if(qr.reg!=r||!seen.insert(qr.index).second)throw std::runtime_error("sample_counts list must contain every qubit in one register exactly once");}
        } else throw std::runtime_error("sample_counts expects a qubit or qregfonk(n) register");
        double total=0.0;std::vector<double> weights;weights.reserve(r->amp.size());
        for(C a:r->amp){double w=std::norm(a);if(!std::isfinite(w)||w<0)throw std::runtime_error("sample_counts encountered invalid probability weight");weights.push_back(w);total+=w;}
        if(!std::isfinite(total)||total<EPS)throw std::runtime_error("sample_counts requires a nonzero finite state");
        std::discrete_distribution<std::size_t> dist(weights.begin(),weights.end());
        auto result=new_array();result->values.resize(weights.size(),0.0);
        for(std::uint64_t shot=0;shot<static_cast<std::uint64_t>(rawShots);++shot){const std::size_t ix=dist(rng_);result->values[ix]=as_num(result->values[ix],"sample_counts")+1.0;}
        return result;
    }
    double entropy_bits(const QRef& q){
        const auto& r=*q.reg;const std::size_t mask=std::size_t(1)<<q.index;double total=0.0,p0=0.0,p1=0.0;C coherence{};
        for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if(!(i&mask)){p0+=w;coherence+=std::conj(r.amp[i])*r.amp[i|mask];}else p1+=w;}
        if(!std::isfinite(total)||total<EPS)throw std::runtime_error("entanglement_entropy requires a nonzero finite state");
        const double purity=(p0*p0+p1*p1+2.0*std::norm(coherence))/(total*total);
        const double delta=std::sqrt(std::clamp(2.0*purity-1.0,0.0,1.0));
        const double l1=std::clamp((1.0+delta)/2.0,0.0,1.0),l2=std::clamp((1.0-delta)/2.0,0.0,1.0);
        double h=0.0;if(l1>EPS)h-=l1*std::log2(l1);if(l2>EPS)h-=l2*std::log2(l2);return h;
    }
    std::shared_ptr<Arr> bloch_vector(const QRef& q){
        const auto& r=*q.reg;const std::size_t mask=std::size_t(1)<<q.index;double total=0.0,p0=0.0,p1=0.0;C coherence{};
        for(std::size_t i=0;i<r.amp.size();++i){double w=std::norm(r.amp[i]);total+=w;if(!(i&mask)){p0+=w;coherence+=std::conj(r.amp[i])*r.amp[i|mask];}else p1+=w;}
        if(!std::isfinite(total)||total<EPS)throw std::runtime_error("bloch_vector requires a nonzero finite state");
        auto a=new_array();a->values.emplace_back(2.0*coherence.real()/total);a->values.emplace_back(2.0*coherence.imag()/total);a->values.emplace_back((p0-p1)/total);return a;
    }
    void print_state(const QRef& q){const auto& r=*q.reg;std::cout<<"DRNCQ state ("<<r.n<<" qubits; |state|^2="<<std::setprecision(12)<<norm2(r)<<")\n";std::size_t shown=0;for(std::size_t i=0;i<r.amp.size();++i){if(std::norm(r.amp[i])<1e-14)continue;if(shown++>=64){std::cout<<"  ... remaining amplitudes omitted ...\n";break;}std::cout<<"  "<<std::fixed<<std::setprecision(6)<<r.amp[i].real()<<(r.amp[i].imag()<0?" - ":" + ")<<std::abs(r.amp[i].imag())<<"i |";for(int qn=r.n-1;qn>=0;--qn){if(qn!=r.n-1)std::cout<<", ";std::cout<<(((i>>qn)&1)?r.sectors[static_cast<std::size_t>(qn)].hayir_label:r.sectors[static_cast<std::size_t>(qn)].evet_label);}std::cout<<">  p="<<std::setprecision(8)<<std::norm(r.amp[i])<<"\n";}}
    V call(const BI& ins,const std::vector<V>& args){const std::string& f=ins.text;auto checked=[&](double x){require_finite(x,f.c_str());return x;};auto arity=[&](std::size_t n){if(args.size()!=n)throw std::runtime_error(f+" expects "+std::to_string(n)+" argument(s)");};auto num=[&](std::size_t i){return as_num(args.at(i),f);};auto ref=[&](std::size_t i){return qref(args.at(i),f);};
        if(f=="print"||f=="yaz"){arity(1);if(auto q=std::get_if<QRef>(&args[0]))print_state(*q);else std::cout<<value_string(args[0])<<"\n";return false;}
        if(f=="type"){arity(1);return type_name(args[0]);}
        if(f=="len"){arity(1);if(auto a=std::get_if<std::shared_ptr<Arr>>(&args[0]))return static_cast<double>((*a)->values.size());if(auto t=std::get_if<std::string>(&args[0]))return static_cast<double>(t->size());throw std::runtime_error("len expects a list or string");}
        if(f=="to_string"){arity(1);return value_string(args[0]);}
        if(f=="to_number"){arity(1);if(is_num(args[0]))return std::get<double>(args[0]);if(auto b=std::get_if<bool>(&args[0]))return *b?1.0:0.0;if(auto t=std::get_if<std::string>(&args[0])){char* end=nullptr;double v=std::strtod(t->c_str(),&end);if(!end||end==t->c_str()||*end||!std::isfinite(v))throw std::runtime_error("to_number cannot parse '"+*t+"'");return v;}throw std::runtime_error("to_number expects number, bool, or string");}
        if(f=="get"){arity(2);auto a=as_array(args[0],f);auto ix=as_index(args[1],f);if(ix>=a->values.size())throw std::runtime_error("list index out of bounds");return a->values[ix];}
        if(f=="set"){arity(3);auto a=as_array(args[0],f);auto ix=as_index(args[1],f);if(ix>=a->values.size())throw std::runtime_error("list index out of bounds");if(would_create_cycle(args[2],a.get(),nullptr))throw std::runtime_error("container reference cycle is not permitted");a->values[ix]=args[2];return args[2];}
        if(f=="push"){arity(2);auto a=as_array(args[0],f);if(a->values.size()>=1000000)throw std::runtime_error("list size limit exceeded");if(would_create_cycle(args[1],a.get(),nullptr))throw std::runtime_error("container reference cycle is not permitted");a->values.push_back(args[1]);return static_cast<double>(a->values.size());}
        if(f=="pop"){arity(1);auto a=as_array(args[0],f);if(a->values.empty())throw std::runtime_error("pop from empty list");V v=a->values.back();a->values.pop_back();return v;}
        if(f=="input"){if(args.size()>1)throw std::runtime_error("input expects zero or one argument");if(!args.empty())std::cout<<value_string(args[0])<<std::flush;std::string line;if(!std::getline(std::cin,line)){if(std::cin.eof())return std::string();throw std::runtime_error("input failed");}return line;}
        if(f=="read_file"){arity(1);auto path=std::get_if<std::string>(&args[0]);if(!path)throw std::runtime_error("read_file path must be a string");return read_text(*path);}
        if(f=="write_file"){arity(2);auto path=std::get_if<std::string>(&args[0]);auto content=std::get_if<std::string>(&args[1]);if(!path||!content)throw std::runtime_error("write_file expects (string path, string content)");if(content->size()>MAX_SOURCE)throw std::runtime_error("write_file content exceeds 8 MiB limit");std::ofstream out(*path,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("cannot open output file '"+*path+"'");out.write(content->data(),static_cast<std::streamsize>(content->size()));if(!out)throw std::runtime_error("failed writing file '"+*path+"'");return static_cast<double>(content->size());}
        if(f=="file_exists"){arity(1);auto path=std::get_if<std::string>(&args[0]);if(!path)throw std::runtime_error("file_exists path must be a string");std::error_code ec;bool yes=std::filesystem::exists(*path,ec);if(ec)throw std::runtime_error("file_exists failed: "+ec.message());return yes;}
        if(f=="upper"||f=="lower"){arity(1);auto t=std::get_if<std::string>(&args[0]);if(!t)throw std::runtime_error(f+" expects a string");std::string out=*t;for(char& c:out){unsigned char u=static_cast<unsigned char>(c);c=static_cast<char>(f=="upper"?std::toupper(u):std::tolower(u));}return out;}
        if(f=="contains"){arity(2);if(auto a=std::get_if<std::string>(&args[0])){auto b=std::get_if<std::string>(&args[1]);if(!b)throw std::runtime_error("contains(string, value) requires a string search value");return a->find(*b)!=std::string::npos;}if(auto a=std::get_if<std::shared_ptr<Arr>>(&args[0])){for(const auto& v:(*a)->values){if(v.index()!=args[1].index())continue;if(std::holds_alternative<std::monostate>(v))return true;if(is_num(v)&&std::get<double>(v)==std::get<double>(args[1]))return true;if(auto x=std::get_if<std::string>(&v)){if(*x==std::get<std::string>(args[1]))return true;}if(auto x=std::get_if<bool>(&v)){if(*x==std::get<bool>(args[1]))return true;}if(auto x=std::get_if<QRef>(&v)){auto y=std::get<QRef>(args[1]);if(x->reg==y.reg&&x->index==y.index)return true;}if(auto x=std::get_if<std::shared_ptr<Arr>>(&v)){if(*x==std::get<std::shared_ptr<Arr>>(args[1]))return true;}if(auto x=std::get_if<std::shared_ptr<Obj>>(&v)){if(*x==std::get<std::shared_ptr<Obj>>(args[1]))return true;}}return false;}throw std::runtime_error("contains expects a string or list as its first argument");}
        if(f=="substr"){if(args.size()<2||args.size()>3)throw std::runtime_error("substr expects 2 or 3 arguments");auto t=std::get_if<std::string>(&args[0]);if(!t)throw std::runtime_error("substr expects a string as its first argument");std::size_t begin=as_index(args[1],f);if(begin>t->size())throw std::runtime_error("substring start out of range");std::size_t count=t->size()-begin;if(args.size()==3)count=std::min(count,as_index(args[2],f));return t->substr(begin,count);}
        if(f=="split"){arity(2);auto t=std::get_if<std::string>(&args[0]);auto sep=std::get_if<std::string>(&args[1]);if(!t||!sep)throw std::runtime_error("split expects (string, string)");if(sep->empty())throw std::runtime_error("split separator cannot be empty");auto a=new_array();std::size_t pos=0;while(true){auto next=t->find(*sep,pos);a->values.emplace_back(t->substr(pos,next==std::string::npos?std::string::npos:next-pos));if(next==std::string::npos)break;pos=next+sep->size();if(a->values.size()>1000000)throw std::runtime_error("split result exceeds list size limit");}return a;}
        if(f=="join"){arity(2);auto a=as_array(args[0],f);auto sep=std::get_if<std::string>(&args[1]);if(!sep)throw std::runtime_error("join separator must be a string");std::string out;for(std::size_t i=0;i<a->values.size();++i){auto item=std::get_if<std::string>(&a->values[i]);if(!item)throw std::runtime_error("join list must contain only strings");if(i)out+=*sep;out+=*item;}return out;}
        if(f=="assert"){if(args.empty()||args.size()>2)throw std::runtime_error("assert expects one or two arguments");if(!truth(args[0]))throw std::runtime_error(args.size()==2?value_string(args[1]):"assertion failed");return true;}
        if(f=="pi"){arity(0);return PI;}
        if(f=="abs"){arity(1);return checked(std::abs(num(0)));}if(f=="sqrt"){arity(1);if(num(0)<0)throw std::runtime_error("sqrt of negative real value");return checked(std::sqrt(num(0)));}if(f=="sin"){arity(1);return checked(std::sin(num(0)));}if(f=="cos"){arity(1);return checked(std::cos(num(0)));}if(f=="tan"){arity(1);return checked(std::tan(num(0)));}if(f=="asin"){arity(1);return checked(std::asin(num(0)));}if(f=="acos"){arity(1);return checked(std::acos(num(0)));}if(f=="atan"){arity(1);return checked(std::atan(num(0)));}if(f=="exp"){arity(1);return checked(std::exp(num(0)));}if(f=="log"){arity(1);if(num(0)<=0)throw std::runtime_error("log requires a positive value");return checked(std::log(num(0)));}if(f=="log10"){arity(1);if(num(0)<=0)throw std::runtime_error("log10 requires a positive value");return checked(std::log10(num(0)));}if(f=="pow"){arity(2);double z=std::pow(num(0),num(1));require_finite(z,"pow");return z;}if(f=="round"){arity(1);return checked(std::round(num(0)));}if(f=="floor"){arity(1);return checked(std::floor(num(0)));}if(f=="ceil"){arity(1);return checked(std::ceil(num(0)));}if(f=="min"||f=="max"){if(args.empty())throw std::runtime_error(f+" expects at least one argument");double v=num(0);for(std::size_t i=1;i<args.size();++i)v=f=="min"?std::min(v,num(i)):std::max(v,num(i));return v;}
        if(f=="hgate"||f=="xgate"||f=="ygate"||f=="zgate"||f=="sgate"||f=="tgate"){arity(1);QRef q=ref(0);if(f=="hgate")h_gate(*q.reg,q.index);else if(f=="xgate")x_gate(*q.reg,q.index);else if(f=="ygate")y_gate(*q.reg,q.index);else if(f=="zgate")z_gate(*q.reg,q.index);else if(f=="sgate")s_gate(*q.reg,q.index);else t_gate(*q.reg,q.index);modular_advance();return true;}
        if(f=="barrier"){arity(1);if(auto q=std::get_if<QRef>(&args[0])){(void)q;}else if(auto ap=std::get_if<std::shared_ptr<Arr>>(&args[0])){if(!*ap)throw std::runtime_error("barrier requires a qubit or register list");for(const auto& v:(*ap)->values)(void)qref(v,f);}else throw std::runtime_error("barrier expects a qubit or register list");return true;}
        if(f=="controlledgate"){
            if(args.size()!=3&&args.size()!=4) throw std::runtime_error("controlledgate(gateName, control, target [, angle]) expects 3 or 4 arguments");
            auto name=std::get_if<std::string>(&args[0]);if(!name)throw std::runtime_error("controlledgate first argument must be a gate-name string");QRef c=ref(1),t=ref(2);if(c.reg==t.reg&&c.index==t.index)throw std::runtime_error("controlledgate control and target must differ");double theta=args.size()==4?num(3):0.0;U2 u=single_matrix(*name,theta,args.size()==4,false);if(c.reg!=t.reg){if(ins.refs.size()<3||ins.refs[1].empty()||ins.refs[2].empty())throw std::runtime_error("controlledgate needs directly rebindable qubit variables/register slots when merging registers; otherwise use qregfonk(n)");(void)merge(c,t);c=qref(get_ref_value(ins.refs[1]),f);t=qref(get_ref_value(ins.refs[2]),f);}apply_u2(*t.reg,t.index,u,c.index);modular_advance();return true;
        }
        if(f=="adjointgate"){
            if(args.size()<2||args.size()>4) throw std::runtime_error("adjointgate(gateName, qubit [, angle]) or adjointgate(twoQubitGate, q1, q2 [, angle])");
            auto name=std::get_if<std::string>(&args[0]);if(!name)throw std::runtime_error("adjointgate first argument must be a gate-name string");
            const std::string g=*name;
            if(g=="cxgate"||g=="czgate"||g=="swapgate") {arity(3);QRef a=ref(1),b=ref(2);if(a.reg==b.reg&&a.index==b.index)throw std::runtime_error("adjoint two-qubit gate requires distinct operands");if(a.reg!=b.reg){if(ins.refs.size()<3||ins.refs[1].empty()||ins.refs[2].empty())throw std::runtime_error("adjointgate requires rebindable operands when merging registers");(void)merge(a,b);a=qref(get_ref_value(ins.refs[1]),f);b=qref(get_ref_value(ins.refs[2]),f);}if(g=="cxgate")cx_gate(*a.reg,a.index,b.index);else if(g=="czgate")cz_gate(*a.reg,a.index,b.index);else swap_gate(*a.reg,a.index,b.index);modular_advance();return true;}
            if(g=="cphasegate"){arity(4);QRef a=ref(1),b=ref(2);double theta=num(3);if(a.reg==b.reg&&a.index==b.index)throw std::runtime_error("adjoint cphasegate requires distinct qubits");if(a.reg!=b.reg){if(ins.refs.size()<3||ins.refs[1].empty()||ins.refs[2].empty())throw std::runtime_error("adjointgate requires rebindable operands when merging registers");(void)merge(a,b);a=qref(get_ref_value(ins.refs[1]),f);b=qref(get_ref_value(ins.refs[2]),f);}cphase_gate(*a.reg,a.index,b.index,-theta);modular_advance();return true;}
            if(g=="ccxgate"||g=="toffoligate"){arity(4);QRef a=ref(1),b=ref(2),t=ref(3);if(a.reg!=b.reg||a.reg!=t.reg)throw std::runtime_error("adjoint Toffoli requires all qubits in one shared register");toffoli_gate(*a.reg,a.index,b.index,t.index);modular_advance();return true;}
            if(args.size()>3) throw std::runtime_error("single-qubit adjointgate accepts at most one optional angle");
            QRef q=ref(1);double theta=args.size()==3?num(2):0.0;U2 u=single_matrix(g,theta,args.size()==3,true);apply_u2(*q.reg,q.index,u);modular_advance();return true;
        }
        if(f=="powergate"){
            if(args.size()!=3&&args.size()!=4) throw std::runtime_error("powergate(gateName, qubit, integerPower [, angle]) expects 3 or 4 arguments");
            auto name=std::get_if<std::string>(&args[0]);if(!name)throw std::runtime_error("powergate first argument must be a gate-name string");double rawPower=num(2);if(!std::isfinite(rawPower)||std::floor(rawPower)!=rawPower||rawPower< -32||rawPower>32)throw std::runtime_error("powergate exponent must be an integer in [-32,32]");QRef q=ref(1);double theta=args.size()==4?num(3):0.0;U2 u=single_matrix(*name,theta,args.size()==4,false);apply_u2(*q.reg,q.index,power_u2(u,static_cast<int>(rawPower)));modular_advance();return true;
        }
        if(f=="measure_basis"){
            arity(2);QRef q=ref(0);auto b=std::get_if<std::string>(&args[1]);if(!b)throw std::runtime_error("measure_basis basis must be \"X\", \"Y\", or \"Z\"");if(*b!="X"&&*b!="Y"&&*b!="Z")throw std::runtime_error("measure_basis basis must be X, Y, or Z");if(*b=="X")h_gate(*q.reg,q.index);else if(*b=="Y"){phase_gate(*q.reg,q.index,-PI/2);h_gate(*q.reg,q.index);}int out=measure_bit(*q.reg,q.index);if(*b=="X")h_gate(*q.reg,q.index);else if(*b=="Y"){h_gate(*q.reg,q.index);phase_gate(*q.reg,q.index,PI/2);}modular_advance();return out?q.reg->sectors[static_cast<std::size_t>(q.index)].hayir_label:q.reg->sectors[static_cast<std::size_t>(q.index)].evet_label;
        }
        if(f=="expectation_x"||f=="expectation_y"){arity(1);auto a=bloch_vector(ref(0));return as_num(a->values[f=="expectation_x"?0:1],f);}
        if(f=="expectation_pauli"){arity(2);auto b=std::get_if<std::string>(&args[1]);if(!b)throw std::runtime_error("expectation_pauli expects a string basis X, Y, or Z");if(*b=="Z"){QRef q=ref(0);return expectation_z_value(q);}if(*b!="X"&&*b!="Y")throw std::runtime_error("expectation_pauli basis must be X, Y, or Z");auto a=bloch_vector(ref(0));return as_num(a->values[*b=="X"?0:1],f);}
        if(f=="statevector"){arity(1);return statevector_value(ref(0));}
        if(f=="density_matrix"){arity(1);return density_matrix_value(ref(0));}
        if(f=="reduced_density"){arity(1);return reduced_density_value(ref(0));}
        if(f=="purity"){arity(1);return reduced_purity(ref(0));}
        if(f=="fidelity"){arity(2);return pure_state_fidelity(ref(0),ref(1));}
        if(f=="phasegate"||f=="rxgate"||f=="rygate"||f=="rzgate"){arity(2);QRef q=ref(0);double angle=num(1);if(f=="phasegate")phase_gate(*q.reg,q.index,angle);else if(f=="rxgate")rx_gate(*q.reg,q.index,angle);else if(f=="rygate")ry_gate(*q.reg,q.index,angle);else rz_gate(*q.reg,q.index,angle);modular_advance();return true;}
        if(f=="cxgate"||f=="czgate"||f=="swapgate"){arity(2);QRef a=ref(0),b=ref(1);if(a.reg==b.reg&&a.index==b.index)throw std::runtime_error(f+" needs two distinct qubits");if(a.reg!=b.reg){if(ins.refs.size()!=2||ins.refs[0].empty()||ins.refs[1].empty())throw std::runtime_error(f+" can merge independent registers only when operands are rebindable variables or constant-index register slots; use qregfonk(n) for indexed operands");(void)merge(a,b);a=qref(get_ref_value(ins.refs[0]),f);b=qref(get_ref_value(ins.refs[1]),f);}if(f=="cxgate")cx_gate(*a.reg,a.index,b.index);else if(f=="czgate")cz_gate(*a.reg,a.index,b.index);else swap_gate(*a.reg,a.index,b.index);modular_advance();return true;}
        if(f=="cphasegate"){arity(3);QRef a=ref(0),b=ref(1);double theta=num(2);if(a.reg==b.reg&&a.index==b.index)throw std::runtime_error("cphasegate needs two distinct qubits");if(a.reg!=b.reg){if(ins.refs.size()<2||ins.refs[0].empty()||ins.refs[1].empty())throw std::runtime_error("cphasegate on separate registers needs direct variables or constant-index register slots; otherwise create qregfonk(n)");(void)merge(a,b);a=qref(get_ref_value(ins.refs[0]),f);b=qref(get_ref_value(ins.refs[1]),f);}cphase_gate(*a.reg,a.index,b.index,theta);modular_advance();return true;}
        if(f=="mcxgate"||f=="mcphasegate"||f=="mczgate"){
            if(f=="mcxgate")arity(2);else if(f=="mcphasegate")arity(3);else arity(2);
            auto controls=control_refs(args[0],f);QRef target=ref(1);for(const auto& c:controls){if(c.reg!=target.reg)throw std::runtime_error(f+" requires all controls and target in the same qregfonk register");}
            std::vector<int> indices;indices.reserve(controls.size());for(const auto& c:controls)indices.push_back(c.index);
            if(f=="mcxgate")mcx_gate(*target.reg,indices,target.index);else if(f=="mcphasegate")mcphase_gate(*target.reg,indices,target.index,num(2));else mcphase_gate(*target.reg,indices,target.index,PI);
            modular_advance();return true;
        }
        if(f=="ccxgate"||f=="toffoligate"){arity(3);QRef a=ref(0),b=ref(1),t=ref(2);if(a.reg!=b.reg||a.reg!=t.reg)throw std::runtime_error("Toffoli operands must belong to one shared register; create them with qregfonk(n)");toffoli_gate(*a.reg,a.index,b.index,t.index);modular_advance();return true;}
        if(f=="show_basis"){arity(0);std::cout<<"DRNC-generated sectors: EVET, HAYIR\nTwo-register composition: (EVET,EVET), (EVET,HAYIR), (HAYIR,EVET), (HAYIR,HAYIR)\nThe basis is generated internally; bit-string literals are not part of DRNC-Q.\n";return false;}
        if(f=="modular_status"){arity(0);std::cout<<"DRNC modular flow ACTIVE\ns = "<<std::setprecision(12)<<modular_s_<<" [dimensionless internal relational parameter]\nmodular steps = "<<mod_steps_<<"\nR-sector energies = {1, sqrt(2)}; branch spectrum = {0, +sqrt(3), -sqrt(3)}\nLogical gates are ideal; explicit evrimfonk/moduleradim applies spectral phase evolution.\nNo external clock is assumed. s is not calibrated to seconds.\n";return modular_s_;}
        if(f=="moduleradim"){if(args.size()>1)throw std::runtime_error("moduleradim expects zero or one argument");modular_advance(args.empty()?1.0:num(0),true);return modular_s_;}
        if(f=="superposfonk"){if(args.size()>1)throw std::runtime_error("superposfonk expects zero or one phase argument");double phase=args.empty()?0.0:num(0);auto r=new_reg(1,{C(1/std::sqrt(2.0),0),std::polar(1/std::sqrt(2.0),phase)});modular_advance();return QRef{r,0};}
        if(f=="qubitfonk"){arity(0);auto r=new_reg(1,{C(1,0),C(0,0)});modular_advance();return QRef{r,0};}
        if(f=="qregfonk"){arity(1);double raw=num(0);if(raw<1||std::floor(raw)!=raw||raw>MAX_QUBITS)throw std::runtime_error("qregfonk(n) requires an integer n in 1..16");int n=static_cast<int>(raw);std::vector<C> amp(std::size_t(1)<<n,C{});amp[0]=C(1,0);auto r=new_reg(n,amp);auto arr=new_array();arr->values.reserve(static_cast<std::size_t>(n));for(int i=0;i<n;++i)arr->values.emplace_back(QRef{r,i});modular_advance();return arr;}
        if(f=="seed"){arity(1);double raw=num(0);if(raw<0||std::floor(raw)!=raw||raw>9007199254740991.0)throw std::runtime_error("seed requires a non-negative integer no larger than 2^53-1");rng_.seed(static_cast<std::uint64_t>(raw));return true;}
        if(f=="random"){arity(0);std::uniform_real_distribution<double> dist(0.0,1.0);return dist(rng_);}
        if(f=="randint"){arity(1);double raw=num(0);if(raw<1||std::floor(raw)!=raw||raw>1000000000.0)throw std::runtime_error("randint(n) requires a positive integer n <= 1e9 and returns [0,n)");std::uniform_int_distribution<std::uint64_t> dist(0,static_cast<std::uint64_t>(raw)-1);return static_cast<double>(dist(rng_));}
        if(f=="donfonk"){
            if(args.empty()){arity(0);auto r=new_reg(2,{C(1/std::sqrt(2.0),0),C(0,0),C(0,0),C(1/std::sqrt(2.0),0)});modular_advance();return QRef{r,0};}
            arity(2);QRef a=ref(0),b=ref(1);if(a.index==b.index&&a.reg==b.reg)throw std::runtime_error("cannot entangle a qubit with itself");std::shared_ptr<QReg> r;
            if(a.reg==b.reg){r=a.reg;}
            else {if(ins.refs.size()!=2||ins.refs[0].empty()||ins.refs[1].empty())throw std::runtime_error("donfonk can merge independent registers only when both references can be rebound; use qregfonk(n) for dynamic indexed operands");(void)merge(a,b);a=qref(get_ref_value(ins.refs[0]),f);b=qref(get_ref_value(ins.refs[1]),f);r=a.reg;}
            h_gate(*r,a.index);cx_gate(*r,a.index,b.index);modular_advance();return true;
        }
        if(f=="partner"){arity(1);QRef q=ref(0);if(q.reg->n!=2)throw std::runtime_error("partner requires a two-qubit register");int n=(q.index==0?1:0);return QRef{q.reg,n};}
        if(f=="zeryafonk"){arity(1);QRef q=ref(0);QRef added=append_zero(q);added.reg->sectors[static_cast<std::size_t>(added.index)]=QSector{SQRT3,-SQRT3,"DAL+","DAL-"};q=qref(args[0],f);cx_gate(*added.reg,q.index,added.index);modular_advance();return added;}
        if(f=="evrimfonk"){if(args.size()<1||args.size()>2)throw std::runtime_error("evrimfonk(q [, ds]) expects one or two arguments");(void)ref(0);modular_advance(args.size()==2?num(1):1.0,true);return true;}
        if(f=="norm"){arity(1);return norm2(*ref(0).reg);}
        if(f=="assert_normalized"){arity(1);double n=norm2(*ref(0).reg);if(std::abs(n-1)>1e-8)throw std::runtime_error("state normalization failed: norm^2="+std::to_string(n));std::cout<<"PASS normalized: norm^2="<<std::setprecision(12)<<n<<"\n";return true;}
        if(f=="verify_entanglement"){if(args.size()!=1&&args.size()!=2)throw std::runtime_error("verify_entanglement(q [, partner]) expects one or two arguments");QRef q=ref(0);if(args.size()==2){QRef p=ref(1);if(q.reg!=p.reg||q.index==p.index)throw std::runtime_error("the supplied variables are not two qubits of the same relational register");}double purity=reduced_purity(q);bool entangled=purity<1.0-1e-7;std::cout<<(entangled?"ENTANGLED":"NOT ENTANGLED")<<"; reduced-state purity="<<std::setprecision(12)<<purity<<"\n";return entangled;}
        if(f=="entanglement_entropy"){arity(1);return entropy_bits(ref(0));}
        if(f=="bloch_vector"){arity(1);return bloch_vector(ref(0));}
        if(f=="sample_counts"){arity(2);return sample_counts(args[0],num(1));}
        if(f=="bit_flip_noise"||f=="phase_flip_noise"||f=="depolarizing_noise"||f=="phase_damping_noise"){
            arity(2);QRef q=ref(0);double p=checked_probability(num(1),f);
            if(f=="bit_flip_noise"){if(random_event(p))x_gate(*q.reg,q.index);}
            else if(f=="phase_flip_noise"){if(random_event(p))z_gate(*q.reg,q.index);}
            else if(f=="depolarizing_noise"){
                if(random_event(p)){std::uniform_int_distribution<int> pauli(0,2);int which=pauli(rng_);if(which==0)x_gate(*q.reg,q.index);else if(which==1)y_gate(*q.reg,q.index);else z_gate(*q.reg,q.index);}
            } else {if(random_event(p/2.0))z_gate(*q.reg,q.index);}
            modular_advance();return true;
        }
        if(f=="amplitude_damping"){arity(2);QRef q=ref(0);amplitude_damping(*q.reg,q.index,num(1));modular_advance();return true;}
        if(f=="measure"){arity(1);QRef q=ref(0);int outcome=measure_bit(*q.reg,q.index);std::string label=outcome?q.reg->sectors[static_cast<std::size_t>(q.index)].hayir_label:q.reg->sectors[static_cast<std::size_t>(q.index)].evet_label;modular_advance();std::cout<<"measurement("<<label<<")\n";return label;}
        if(f=="probability"){arity(2);QRef q=ref(0);if(!std::holds_alternative<std::string>(args[1]))throw std::runtime_error("probability second argument must be a sector-label string");std::string label=std::get<std::string>(args[1]);const auto& sec=q.reg->sectors[static_cast<std::size_t>(q.index)];int bit=-1;if(label==sec.evet_label)bit=0;else if(label==sec.hayir_label)bit=1;else throw std::runtime_error("sector label for this qubit must be \""+sec.evet_label+"\" or \""+sec.hayir_label+"\"");double p=0;for(std::size_t i=0;i<q.reg->amp.size();++i)if(static_cast<int>((i>>q.index)&1)==bit)p+=std::norm(q.reg->amp[i]);return p;}
        if(f=="probabilities"){arity(1);QRef q=ref(0);auto a=new_array();a->values.reserve(q.reg->amp.size());for(C amp:q.reg->amp)a->values.emplace_back(std::norm(amp));return a;}
        if(f=="expectation_z"){arity(1);QRef q=ref(0);double p0=0,p1=0;for(std::size_t i=0;i<q.reg->amp.size();++i){if((i>>q.index)&1)p1+=std::norm(q.reg->amp[i]);else p0+=std::norm(q.reg->amp[i]);}return p0-p1;}
        if(f=="dump_state"){arity(1);QRef q=ref(0);print_state(q);return true;}
        if(f=="register_size"){arity(1);if(auto q=std::get_if<QRef>(&args[0]))return static_cast<double>(q->reg->n);if(auto ap=std::get_if<std::shared_ptr<Arr>>(&args[0])){if(!*ap||(*ap)->values.empty())return 0.0;return static_cast<double>(qref((*ap)->values.front(),f).reg->n);}throw std::runtime_error("register_size expects a qubit or qubit-register list");}
        if(f=="resetqubit"){arity(1);QRef q=ref(0);int out=measure_bit(*q.reg,q.index);if(out)x_gate(*q.reg,q.index);modular_advance();return true;}
        if(f=="measure_all"){
            arity(1);std::shared_ptr<QReg> r;if(auto q=std::get_if<QRef>(&args[0]))r=q->reg;else if(auto ap=std::get_if<std::shared_ptr<Arr>>(&args[0])){if(!*ap||(*ap)->values.empty())throw std::runtime_error("measure_all requires a non-empty qubit register");r=qref((*ap)->values.front(),f).reg;if((*ap)->values.size()!=static_cast<std::size_t>(r->n))throw std::runtime_error("measure_all list must contain every qubit in the register");std::set<int> seen;for(const auto& v:(*ap)->values){auto qr=qref(v,f);if(qr.reg!=r||!seen.insert(qr.index).second)throw std::runtime_error("measure_all list must contain every qubit in one register exactly once");}}else throw std::runtime_error("measure_all expects a qubit or qregfonk(n) list");
            double total=norm2(*r);if(!std::isfinite(total)||total<EPS)throw std::runtime_error("cannot measure a zero-norm state");std::uniform_real_distribution<double> dist(0.0,total);double roll=dist(rng_),acc=0;std::size_t picked=r->amp.size()-1;for(std::size_t i=0;i<r->amp.size();++i){acc+=std::norm(r->amp[i]);if(roll<=acc){picked=i;break;}}for(std::size_t i=0;i<r->amp.size();++i)r->amp[i]=(i==picked?C(1,0):C(0,0));auto out=new_array();std::string joined;for(int qi=r->n-1;qi>=0;--qi){int bit=static_cast<int>((picked>>qi)&1);const auto& sec=r->sectors[static_cast<std::size_t>(qi)];std::string label=bit?sec.hayir_label:sec.evet_label;out->values.emplace_back(label);if(!joined.empty())joined+=" | ";joined+=label;}std::cout<<"measurement_all("<<joined<<")\n";modular_advance();return out;
        }
        if(f=="teleportfonk"){
            arity(2);if(ins.refs.size()!=2||ins.refs[0].empty()||ins.refs[1].empty())throw std::runtime_error("teleportfonk(source, destination) requires two variable references");QRef src=ref(0),dst=ref(1);if(src.reg==dst.reg&&src.index==dst.index)throw std::runtime_error("source and destination must be different qubits");(void)merge(src,dst);src=qref(get_ref_value(ins.refs[0]),f);dst=qref(get_ref_value(ins.refs[1]),f);QRef aux=append_zero(dst);src=qref(get_ref_value(ins.refs[0]),f);dst=qref(get_ref_value(ins.refs[1]),f);aux={src.reg,aux.index};h_gate(*aux.reg,dst.index);cx_gate(*aux.reg,dst.index,aux.index);cx_gate(*aux.reg,src.index,dst.index);h_gate(*aux.reg,src.index);int msrc=measure_bit(*aux.reg,src.index);int mbell=measure_bit(*aux.reg,dst.index);if(mbell)x_gate(*aux.reg,aux.index);if(msrc)z_gate(*aux.reg,aux.index);set_ref_value(ins.refs[1],QRef{aux.reg,aux.index});modular_advance();const std::string src_label=msrc?aux.reg->sectors[static_cast<std::size_t>(src.index)].hayir_label:aux.reg->sectors[static_cast<std::size_t>(src.index)].evet_label;const std::string bell_label=mbell?aux.reg->sectors[static_cast<std::size_t>(dst.index)].hayir_label:aux.reg->sectors[static_cast<std::size_t>(dst.index)].evet_label;std::cout<<"DRNCQ logical teleportation complete: "<<ins.refs[0]<<" -> "<<ins.refs[1]<<"; classical outcomes="<<src_label<<","<<bell_label<<"; modular s="<<modular_s_<<"\n";return true;
        }
        if(f=="same_register"){arity(2);auto a=ref(0),b=ref(1);return a.reg==b.reg;}
        if(f=="__assign"){arity(2);auto n=std::get_if<std::string>(&args[0]);if(!n)throw std::runtime_error("internal for increment error");set_value(*n,args[1]);return args[1];}
        throw std::runtime_error("unknown function '"+f+"'");
    }
    void step(const BI& i){switch(i.op){
        case BO::PushNum:push(i.num);break;case BO::PushStr:push(i.text);break;case BO::PushBool:push(i.a!=0);break;case BO::PushNull:push(std::monostate{});break;
        case BO::Load:{if(i.text=="pi"){push(PI);break;}if(i.text=="modular_s"){push(modular_s_);break;}push(get_value(i.text));break;}
        case BO::Declare:{V v=pop();declare_value(i.text,std::move(v));break;}case BO::Store:{V v=pop();set_value(i.text,std::move(v));break;}case BO::Pop:(void)pop();break;
        case BO::Pos:{V v=pop();push(as_num(v,"unary plus"));break;}case BO::Neg:{V v=pop();push(-as_num(v,"unary minus"));break;}case BO::Not:{V v=pop();push(!truth(v));break;}case BO::ToBool:{V v=pop();push(truth(v));break;}
        case BO::Add:case BO::Sub:case BO::Mul:case BO::Div:case BO::Mod:case BO::Pow:case BO::Eq:case BO::Ne:case BO::Lt:case BO::Le:case BO::Gt:case BO::Ge:{V b=pop(),a=pop();if(i.text=="&&")push(truth(a)&&truth(b));else if(i.text=="||")push(truth(a)||truth(b));else push(numeric_binary(i.op,a,b,i.text));break;}
        case BO::Call:{if(i.a>64)throw std::runtime_error("function argument count exceeds 64");std::vector<V>a(i.a);for(std::size_t n=i.a;n>0;--n)a[n-1]=pop();push(call(i,a));break;}
        case BO::CallUser:{
            auto f=program_.functions.find(i.text);if(f==program_.functions.end())throw std::runtime_error("unknown function '"+i.text+"'");
            if(f->second.params.size()!=i.a||f->second.modes.size()!=i.a)throw std::runtime_error("function argument count/mode mismatch for '"+i.text+"'");
            if(calls_.size()>=256)throw std::runtime_error("maximum function recursion depth (256) exceeded");
            std::vector<V>a(i.a);for(std::size_t n=i.a;n>0;--n)a[n-1]=pop();
            CallFrame fr{pc_,stack_.size(),scopes_.size(),handlers_.size(),std::nullopt,{}};
            std::map<std::string,V> local;
            for(std::size_t n=0;n<a.size();++n){
                const auto mode=f->second.modes[n];
                if(mode>2)throw std::runtime_error("invalid parameter mode in bytecode");
                if(mode!=0){if(n>=i.refs.size()||i.refs[n].empty())throw std::runtime_error("output parameter requires a writable argument reference");fr.outputBindings.emplace_back(f->second.params[n],i.refs[n]);}
                local.emplace(f->second.params[n],mode==1?V(std::monostate{}):std::move(a[n]));
            }
            calls_.push_back(std::move(fr));scopes_.push_back(std::move(local));pc_=f->second.entry;break;
        }
        case BO::Jump:if(i.a>program_.code.size())throw std::runtime_error("invalid jump target");pc_=i.a;break;
        case BO::JumpFalse:{V v=pop();if(!truth(v)){if(i.a>program_.code.size())throw std::runtime_error("invalid jump target");pc_=i.a;}break;}
        case BO::MakeList:{if(i.a>100000)throw std::runtime_error("list literal exceeds limit");auto a=new_array();a->values.resize(i.a);for(std::size_t n=i.a;n>0;--n)a->values[n-1]=pop();push(a);break;}
        case BO::IndexGet:{V iv=pop(),av=pop();auto ix=as_index(iv,"indexing");if(auto a=std::get_if<std::shared_ptr<Arr>>(&av)){if(ix>=(*a)->values.size())throw std::runtime_error("list index out of bounds");push((*a)->values[ix]);}else if(auto t=std::get_if<std::string>(&av)){if(ix>=t->size())throw std::runtime_error("string byte index out of bounds");push(std::string(1,(*t)[ix]));}else throw std::runtime_error("indexing requires a list or string");break;}
        case BO::IndexSet:{V val=pop(),iv=pop(),av=pop();auto a=as_array(av,"indexed assignment");auto ix=as_index(iv,"indexed assignment");if(ix>=a->values.size())throw std::runtime_error("list index out of bounds");if(would_create_cycle(val,a.get(),nullptr))throw std::runtime_error("container reference cycle is not permitted");a->values[ix]=std::move(val);break;}
        case BO::Return:{
            if(calls_.empty())throw std::runtime_error("RETURN outside function frame");
            V result=i.a?pop():V(std::monostate{});auto fr=calls_.back();calls_.pop_back();
            std::vector<std::pair<std::string,V>> copyBack;
            if(!fr.outputBindings.empty()){
                if(fr.scopeDepth>=scopes_.size())throw std::runtime_error("missing activation scope for output parameters");
                const auto& activation=scopes_[fr.scopeDepth];
                for(const auto& b:fr.outputBindings){auto v=activation.find(b.first);if(v==activation.end())throw std::runtime_error("output parameter missing from activation: "+b.first);copyBack.emplace_back(b.second,v->second);}
            }
            if(stack_.size()>fr.stackDepth) stack_.resize(fr.stackDepth);
            if(scopes_.size()>fr.scopeDepth) scopes_.resize(fr.scopeDepth);
            if(handlers_.size()>fr.handlerDepth) handlers_.resize(fr.handlerDepth);
            pc_=fr.returnPc;
            for(auto& b:copyBack) set_ref_value(b.first,std::move(b.second));
            if(fr.forcedReturn) result=*fr.forcedReturn;
            push(std::move(result));break;
        }
        case BO::MemberGet:{auto obj=as_object(pop(),"property access");auto f=obj->fields.find(i.text);if(f==obj->fields.end())throw std::runtime_error("undefined property '"+i.text+"' on "+obj->className);push(f->second);break;}
        case BO::MemberSet:{V value=pop();auto obj=as_object(pop(),"property assignment");if(would_create_cycle(value,nullptr,obj.get()))throw std::runtime_error("container reference cycle is not permitted");obj->fields[i.text]=std::move(value);break;}
        case BO::CallMethod:{if(i.a>64)throw std::runtime_error("method argument count exceeds 64");std::vector<V> args(i.a);for(std::size_t n=i.a;n>0;--n)args[n-1]=pop();auto obj=as_object(pop(),"method call");const std::string full=obj->className+"."+i.text;auto f=program_.functions.find(full);if(f==program_.functions.end())throw std::runtime_error("unknown method '"+full+"'");if(f->second.params.size()!=args.size()+1)throw std::runtime_error("method argument count mismatch for '"+full+"'");if(calls_.size()>=256)throw std::runtime_error("maximum function recursion depth (256) exceeded");CallFrame fr{pc_,stack_.size(),scopes_.size(),handlers_.size(),std::nullopt};calls_.push_back(fr);std::map<std::string,V> local;local.emplace(f->second.params[0],obj);for(std::size_t n=0;n<args.size();++n)local.emplace(f->second.params[n+1],std::move(args[n]));scopes_.push_back(std::move(local));pc_=f->second.entry;break;}
        case BO::CallClass:{if(i.a>64)throw std::runtime_error("constructor argument count exceeds 64");if(!program_.classes.count(i.text))throw std::runtime_error("unknown class '"+i.text+"'");std::vector<V> args(i.a);for(std::size_t n=i.a;n>0;--n)args[n-1]=pop();auto obj=new_object(i.text);const std::string ctorName=i.text+".init";auto f=program_.functions.find(ctorName);if(f==program_.functions.end()){if(!args.empty())throw std::runtime_error("class '"+i.text+"' has no init constructor");push(obj);break;}if(f->second.params.size()!=args.size()+1)throw std::runtime_error("constructor argument count mismatch for '"+i.text+"'");if(calls_.size()>=256)throw std::runtime_error("maximum function recursion depth (256) exceeded");CallFrame fr{pc_,stack_.size(),scopes_.size(),handlers_.size(),V(obj)};calls_.push_back(fr);std::map<std::string,V> local;local.emplace(f->second.params[0],obj);for(std::size_t n=0;n<args.size();++n)local.emplace(f->second.params[n+1],std::move(args[n]));scopes_.push_back(std::move(local));pc_=f->second.entry;break;}
        case BO::EnterScope:scopes_.emplace_back();break;case BO::ExitScope:if(scopes_.empty())throw std::runtime_error("scope stack underflow");scopes_.pop_back();break;
        case BO::TryBegin:handlers_.push_back(Handler{i.a,stack_.size(),scopes_.size(),calls_.size(),i.text});break;case BO::TryEnd:if(handlers_.empty())throw std::runtime_error("exception-handler stack underflow");handlers_.pop_back();break;
        case BO::Throw:{V x=pop();throw std::runtime_error(value_string(x));}
        case BO::Halt:break;
    }}
    V numeric_binary(BO op,const V& a,const V& b,const std::string& txt){if(txt=="&&")return truth(a)&&truth(b);if(txt=="||")return truth(a)||truth(b);return arithmetic(op,a,b,txt);}
    V arithmetic(BO op,const V& a,const V& b,const std::string& txt){if(op==BO::Add){if(auto x=std::get_if<std::string>(&a))return *x+value_string(b);if(auto y=std::get_if<std::string>(&b))return value_string(a)+*y;}if(op==BO::Eq||op==BO::Ne){bool e=false;if(a.index()==b.index()){if(std::holds_alternative<std::monostate>(a))e=true;else if(is_num(a))e=std::abs(std::get<double>(a)-std::get<double>(b))<EPS;else if(auto xs=std::get_if<std::string>(&a))e=*xs==std::get<std::string>(b);else if(auto xb=std::get_if<bool>(&a))e=*xb==std::get<bool>(b);else if(auto xq=std::get_if<QRef>(&a)){auto yq=std::get<QRef>(b);e=xq->reg==yq.reg&&xq->index==yq.index;}else if(std::holds_alternative<std::shared_ptr<Arr>>(a))e=std::get<std::shared_ptr<Arr>>(a)==std::get<std::shared_ptr<Arr>>(b);else e=std::get<std::shared_ptr<Obj>>(a)==std::get<std::shared_ptr<Obj>>(b);}return op==BO::Eq?e:!e;}if(op==BO::Lt||op==BO::Le||op==BO::Gt||op==BO::Ge){if(auto x=std::get_if<std::string>(&a)){auto y=std::get_if<std::string>(&b);if(!y)throw std::runtime_error("cannot compare string with non-string");if(op==BO::Lt)return *x<*y;if(op==BO::Le)return *x<=*y;if(op==BO::Gt)return *x>*y;return *x>=*y;}}double x=as_num(a,txt),y=as_num(b,txt),z=0;switch(op){case BO::Add:z=x+y;break;case BO::Sub:z=x-y;break;case BO::Mul:z=x*y;break;case BO::Div:if(y==0.0)throw std::runtime_error("division by zero");z=x/y;break;case BO::Mod:if(y==0.0)throw std::runtime_error("modulo by zero");z=std::fmod(x,y);break;case BO::Pow:z=std::pow(x,y);break;case BO::Lt:return x<y;case BO::Le:return x<=y;case BO::Gt:return x>y;case BO::Ge:return x>=y;default:throw std::runtime_error("invalid arithmetic opcode");}require_finite(z,txt.c_str());return z;}
};

// -------------------------- Bytecode file format ---------------------------
static void put_u32(std::vector<std::uint8_t>& b,std::uint32_t x){for(int i=0;i<4;++i)b.push_back((x>>(8*i))&255);}
static void put_u64(std::vector<std::uint8_t>& b,std::uint64_t x){for(int i=0;i<8;++i)b.push_back((x>>(8*i))&255);}
static void put_str(std::vector<std::uint8_t>& b,const std::string& s){if(s.size()>1024*1024)throw std::runtime_error("bytecode string exceeds 1 MiB");put_u32(b,static_cast<std::uint32_t>(s.size()));b.insert(b.end(),s.begin(),s.end());}
static std::uint32_t crc32(const std::uint8_t* data,std::size_t n){std::uint32_t c=0xffffffffu;for(std::size_t i=0;i<n;++i){c^=data[i];for(int k=0;k<8;++k)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
static std::vector<std::uint8_t> encode(const ByteProgram& p){
    std::vector<std::uint8_t>b;const char magic[8]={'D','R','N','Q','B','7','\r','\n'};b.insert(b.end(),magic,magic+8);put_u32(b,7);put_str(b,p.name);
    put_u32(b,static_cast<std::uint32_t>(p.functions.size()));for(const auto& kv:p.functions){put_str(b,kv.first);put_u32(b,kv.second.entry);put_u32(b,static_cast<std::uint32_t>(kv.second.params.size()));for(std::size_t i=0;i<kv.second.params.size();++i){put_str(b,kv.second.params[i]);b.push_back(i<kv.second.modes.size()?kv.second.modes[i]:0);}}
    put_u32(b,static_cast<std::uint32_t>(p.classes.size()));for(const auto& c:p.classes)put_str(b,c);put_u32(b,static_cast<std::uint32_t>(p.code.size()));for(const BI&i:p.code){b.push_back(static_cast<std::uint8_t>(i.op));put_u32(b,i.line);std::uint64_t d;std::memcpy(&d,&i.num,8);put_u64(b,d);put_u32(b,i.a);put_str(b,i.text);put_u32(b,static_cast<std::uint32_t>(i.refs.size()));for(auto& r:i.refs)put_str(b,r);}put_u32(b,crc32(b.data(),b.size()));return b;
}
class Reader{public:explicit Reader(const std::vector<std::uint8_t>&b):b_(b){}std::uint8_t u8(){need(1);return b_[p_++];}std::uint32_t u32(){need(4);std::uint32_t x=0;for(int i=0;i<4;++i)x|=std::uint32_t(b_[p_++])<<(i*8);return x;}std::uint64_t u64(){need(8);std::uint64_t x=0;for(int i=0;i<8;++i)x|=std::uint64_t(b_[p_++])<<(i*8);return x;}std::string str(){auto n=u32();if(n>1024*1024)throw std::runtime_error("bytecode string length exceeds limit");need(n);std::string s(b_.begin()+p_,b_.begin()+p_+n);p_+=n;return s;}std::size_t pos()const{return p_;}private:const std::vector<std::uint8_t>&b_;std::size_t p_{0};void need(std::size_t n){if(n>b_.size()-std::min(p_,b_.size()))throw std::runtime_error("truncated bytecode");}};
static std::vector<std::uint8_t> read_bin(const std::string&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot open bytecode '"+p+"'");f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0||static_cast<std::uint64_t>(n)>64u*1024u*1024u)throw std::runtime_error("bytecode exceeds 64 MiB limit");f.seekg(0);std::vector<std::uint8_t>b(static_cast<std::size_t>(n));if(!b.empty()&&!f.read(reinterpret_cast<char*>(b.data()),n))throw std::runtime_error("cannot read bytecode");return b;}
static ByteProgram decode(const std::string& path){
    auto b=read_bin(path);if(b.size()<20)throw std::runtime_error("bytecode too short");const char m[8]={'D','R','N','Q','B','7','\r','\n'};if(std::memcmp(b.data(),m,8))throw std::runtime_error("invalid DRNQB7 magic (bytecode from older releases is not compatible)");std::uint32_t stored=std::uint32_t(b[b.size()-4])|(std::uint32_t(b[b.size()-3])<<8)|(std::uint32_t(b[b.size()-2])<<16)|(std::uint32_t(b[b.size()-1])<<24);if(crc32(b.data(),b.size()-4)!=stored)throw std::runtime_error("bytecode checksum mismatch");b.resize(b.size()-4);Reader r(b);for(int i=0;i<8;++i)(void)r.u8();if(r.u32()!=7)throw std::runtime_error("unsupported bytecode version");ByteProgram p;p.name=r.str();auto nf=r.u32();if(nf>100000)throw std::runtime_error("invalid function count");for(std::uint32_t j=0;j<nf;++j){std::string name=r.str();FunctionInfo f;f.entry=r.u32();auto np=r.u32();if(np>64)throw std::runtime_error("function parameter count exceeds limit");for(std::uint32_t k=0;k<np;++k){f.params.push_back(r.str());auto mode=r.u8();if(mode>2)throw std::runtime_error("invalid function parameter mode");f.modes.push_back(mode);}if(!p.functions.emplace(name,std::move(f)).second)throw std::runtime_error("duplicate function metadata");}
    auto nc=r.u32();if(nc>100000)throw std::runtime_error("invalid class count");for(std::uint32_t j=0;j<nc;++j){if(!p.classes.insert(r.str()).second)throw std::runtime_error("duplicate class metadata");}auto n=r.u32();if(n==0||n>MAX_INSTRUCTIONS)throw std::runtime_error("invalid instruction count");for(std::uint32_t j=0;j<n;++j){BI i;auto op=r.u8();if(op>static_cast<std::uint8_t>(BO::CallClass))throw std::runtime_error("invalid opcode");i.op=static_cast<BO>(op);i.line=r.u32();std::uint64_t d=r.u64();std::memcpy(&i.num,&d,8);if(!std::isfinite(i.num))throw std::runtime_error("non-finite number in bytecode");i.a=r.u32();i.text=r.str();auto nr=r.u32();if(nr>64)throw std::runtime_error("call reference list too long");for(std::uint32_t k=0;k<nr;++k)i.refs.push_back(r.str());if((i.op==BO::Jump||i.op==BO::JumpFalse||i.op==BO::TryBegin)&&i.a>n)throw std::runtime_error("invalid jump target in bytecode");if((i.op==BO::Call||i.op==BO::CallUser||i.op==BO::CallMethod||i.op==BO::CallClass)&&i.a>64)throw std::runtime_error("invalid call argument count");p.code.push_back(std::move(i));}
    if(r.pos()!=b.size()) throw std::runtime_error("trailing bytes in bytecode");
    if(p.code.back().op!=BO::Halt) throw std::runtime_error("bytecode does not terminate with HALT");
    for(const auto& kv:p.functions) if(kv.second.entry>=p.code.size()) throw std::runtime_error("invalid function entry in bytecode");
    return p;
}
static void write_bin(const std::string& p,const std::vector<std::uint8_t>& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("cannot write '"+p+"'");f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f)throw std::runtime_error("failed to write '"+p+"'");}

static std::string read_source_and_parse(const std::string& path,SourceProgram& parsed,bool requireTarget=false){auto h=extract_target_header(read_text(path));if(requireTarget&&!h.explicitTarget)throw std::runtime_error("missing target selector in '"+path+"': first non-comment code line must be %set[PC] or %set[QPC]");Lexer l(h.body);Parser p(l.scan());parsed=p.parse();parsed.target=h.target;return h.body;}
static void load_module_tree(const std::filesystem::path& path,std::vector<std::filesystem::path>& visiting,std::set<std::filesystem::path>& loaded,SourceProgram& out,bool isRoot=false){
    std::error_code ec;auto abs=std::filesystem::weakly_canonical(path,ec);if(ec)abs=std::filesystem::absolute(path,ec);if(std::find(visiting.begin(),visiting.end(),abs)!=visiting.end())throw std::runtime_error("cyclic module import detected at '"+abs.string()+"'");if(loaded.count(abs))return;SourceProgram cur;read_source_and_parse(abs.string(),cur,isRoot);visiting.push_back(abs);for(const auto& imp:cur.imports){std::filesystem::path ip=std::filesystem::path(imp);if(ip.is_relative())ip=abs.parent_path()/ip;load_module_tree(ip,visiting,loaded,out,false);}visiting.pop_back();loaded.insert(abs);if(isRoot){out.name=cur.name;out.target=cur.target;}out.functions.insert(out.functions.end(),cur.functions.begin(),cur.functions.end());out.classes.insert(out.classes.end(),cur.classes.begin(),cur.classes.end());out.statements.insert(out.statements.end(),cur.statements.begin(),cur.statements.end());
}
static ByteProgram compile_file(const std::string& path){SourceProgram s;std::vector<std::filesystem::path> visiting;std::set<std::filesystem::path> loaded;load_module_tree(path,visiting,loaded,s,true);Compiler c;return c.compile(s);}
static void print_ir(const ByteProgram& p){std::cout<<"DRNC-Q v1.8 DRNQ-IR / stack bytecode\nmodule @"<<p.name<<" {\n  %kernel = drnc.relational_kernel<loop=triangle, root=v_delta>\n  %modular = drnc.modular_flow<external_clock=none, step_unit=dimensionless>\n  %sectors = drnc.generated_sectors<EVET,HAYIR>\n";for(std::size_t i=0;i<p.code.size();++i){const auto& q=p.code[i];std::cout<<"  "<<std::setw(5)<<i<<" [line "<<q.line<<"] "<<VM::opcode_name(q.op);if(!q.text.empty())std::cout<<" "<<q.text;if(q.op==BO::PushNum)std::cout<<" "<<q.num;if(q.op==BO::Jump||q.op==BO::JumpFalse)std::cout<<" -> "<<q.a;if(q.op==BO::Call||q.op==BO::CallUser||q.op==BO::CallMethod||q.op==BO::CallClass)std::cout<<" argc="<<q.a;std::cout<<"\n";}std::cout<<"}\n";}
static void write_text_file(const std::string& path,const std::string& data){std::ofstream f(path,std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("cannot write '"+path+"'");f.write(data.data(),static_cast<std::streamsize>(data.size()));if(!f)throw std::runtime_error("failed to write '"+path+"'");}
static std::string trim_copy(const std::string& s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    if(a==std::string::npos) return {};
    const auto b=s.find_last_not_of(" \t\r\n");
    return s.substr(a,b-a+1);
}
static std::string collapse_ws(const std::string& s) {
    std::string o; bool ws=false;
    for(unsigned char c:s) {
        if(std::isspace(c)) { ws=!o.empty(); continue; }
        if(ws) o.push_back(' ');
        o.push_back(static_cast<char>(c)); ws=false;
    }
    return o;
}
static std::vector<std::size_t> qpc_qubit_refs(const std::string& line) {
    std::vector<std::size_t> out; std::size_t pos=0;
    while((pos=line.find("q[",pos))!=std::string::npos) {
        const auto close=line.find(']',pos+2);
        if(close==std::string::npos) { pos+=2; continue; }
        const std::string digits=line.substr(pos+2,close-(pos+2));
        if(!digits.empty() && std::all_of(digits.begin(),digits.end(),[](unsigned char c){return std::isdigit(c)!=0;})) {
            try { out.push_back(static_cast<std::size_t>(std::stoull(digits))); } catch(...) { }
        }
        pos=close+1;
    }
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
static std::size_t qpc_decl_size(const std::string& line,const std::string& prefix) {
    const auto p=line.find(prefix);if(p==std::string::npos)return 0;
    const auto end=line.find(']',p+prefix.size());if(end==std::string::npos)return 0;
    try{return static_cast<std::size_t>(std::stoull(line.substr(p+prefix.size(),end-(p+prefix.size()))));}catch(...){return 0;}
}
struct QPCReport {
    std::size_t qubits{0},classicalBits{0},quantumStatements{0},measurements{0},twoOrMoreQubitOps{0},conditions{0},customGateDefinitions{0},maxQubitIndex{0},estimatedSerialDepth{0};
    std::map<std::string,std::size_t> operationKinds;
    bool hasQubitIndex{false};
};
static QPCReport qpc_analyze(const std::string& qasm) {
    QPCReport r;std::istringstream in(qasm);std::string line;bool inGateDef=false;int gateBrace=0;std::vector<std::size_t> lastLayer;
    while(std::getline(in,line)) {
        const std::string t=trim_copy(line);if(t.empty()||t.rfind("//",0)==0)continue;
        if(t.rfind("OPENQASM",0)==0||t.rfind("include ",0)==0)continue;
        if(t.rfind("qubit[",0)==0){r.qubits+=qpc_decl_size(t,"qubit[");lastLayer.resize(r.qubits,0);continue;}
        if(t.rfind("bit[",0)==0){r.classicalBits+=qpc_decl_size(t,"bit[");continue;}
        if(!inGateDef && t.rfind("gate ",0)==0 && t.find('{')!=std::string::npos){++r.customGateDefinitions;inGateDef=true;gateBrace=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));continue;}
        if(inGateDef){gateBrace+=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));if(gateBrace<=0)inGateDef=false;continue;}
        if(t.rfind("if ",0)==0||t.rfind("if(",0)==0)++r.conditions;
        const auto refs=qpc_qubit_refs(t);const auto semis=static_cast<std::size_t>(std::count(t.begin(),t.end(),';'));
        if(refs.empty()||semis==0)continue;
        // Generated OpenQASM currently emits one quantum operation per statement line.
        r.quantumStatements+=semis;
        const bool measurement=t.find("measure")!=std::string::npos;
        if(measurement)r.measurements+=semis;
        if(!measurement && refs.size()>=2)r.twoOrMoreQubitOps+=semis;
        std::string kind;
        if(measurement)kind="measure";
        else if(t.rfind("if ",0)==0||t.rfind("if(",0)==0)kind="conditional-operation";
        else if(t.rfind("ctrl @",0)==0)kind="controlled-custom";
        else if(t.rfind("inv @",0)==0)kind="adjoint-custom";
        else {const auto end=t.find_first_of(" (\t");kind=t.substr(0,end);}
        r.operationKinds[kind]+=semis;
        std::size_t layer=1;for(auto qi:refs){r.hasQubitIndex=true;r.maxQubitIndex=std::max(r.maxQubitIndex,qi);if(qi<lastLayer.size())layer=std::max(layer,lastLayer[qi]+1);}
        for(auto qi:refs)if(qi<lastLayer.size())lastLayer[qi]=layer;
        r.estimatedSerialDepth=std::max(r.estimatedSerialDepth,layer);
    }
    return r;
}
struct QPCOptimizationResult {std::string qasm;std::size_t canceledPairs{0},removedIdentityRotations{0};};
static bool qpc_simple_involution(const std::string& line,std::string& canonical) {
    const std::string t=collapse_ws(trim_copy(line));
    static const std::regex one(R"(^(h|x|y|z) q\[[0-9]+\];$)");
    static const std::regex two(R"(^(cx|cz|swap) q\[[0-9]+\], q\[[0-9]+\];$)");
    static const std::regex three(R"(^ccx q\[[0-9]+\], q\[[0-9]+\], q\[[0-9]+\];$)");
    if(std::regex_match(t,one)||std::regex_match(t,two)||std::regex_match(t,three)){canonical=t;return true;}return false;
}
static bool qpc_identity_rotation(const std::string& line) {
    const std::string t=collapse_ws(trim_copy(line));
    static const std::regex rx(R"(^(p|phase|rx|ry|rz)\(([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\) q\[[0-9]+\];$)");
    std::smatch m;if(!std::regex_match(t,m,rx))return false;
    try {const double v=std::stod(m[2].str());return std::isfinite(v)&&std::abs(v)<1e-14;}catch(...){return false;}
}
static QPCOptimizationResult qpc_optimize(const std::string& qasm) {
    QPCOptimizationResult res;std::istringstream in(qasm);std::string line;std::vector<std::string> out;std::optional<std::size_t> previousGate;std::string previousCanonical;bool inGateDef=false;int gateBrace=0;int blockDepth=0;
    while(std::getline(in,line)) {
        const std::string t=trim_copy(line);
        if(inGateDef){out.push_back(line);gateBrace+=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));if(gateBrace<=0){inGateDef=false;previousGate.reset();previousCanonical.clear();}continue;}
        if(t.rfind("gate ",0)==0&&t.find('{')!=std::string::npos){out.push_back(line);inGateDef=true;gateBrace=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));previousGate.reset();previousCanonical.clear();continue;}
        if(blockDepth>0){out.push_back(line);blockDepth+=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));if(blockDepth<=0){blockDepth=0;previousGate.reset();previousCanonical.clear();}continue;}
        if((t.rfind("if ",0)==0||t.rfind("if(",0)==0||t.rfind("for ",0)==0||t.rfind("while ",0)==0)&&t.find('{')!=std::string::npos){out.push_back(line);blockDepth=static_cast<int>(std::count(t.begin(),t.end(),'{'))-static_cast<int>(std::count(t.begin(),t.end(),'}'));previousGate.reset();previousCanonical.clear();continue;}
        if(t.empty()||t.rfind("//",0)==0){out.push_back(line);continue;}
        if(qpc_identity_rotation(t)){++res.removedIdentityRotations;continue;}
        std::string canonical;
        if(qpc_simple_involution(t,canonical)) {
            if(previousGate && previousCanonical==canonical) {out[*previousGate].clear();++res.canceledPairs;previousGate.reset();previousCanonical.clear();continue;}
            out.push_back(line);previousGate=out.size()-1;previousCanonical=canonical;continue;
        }
        out.push_back(line);previousGate.reset();previousCanonical.clear();
        if(t.find('{')!=std::string::npos||t.find('}')!=std::string::npos){previousGate.reset();previousCanonical.clear();}
    }
    std::ostringstream joined;for(const auto& x:out)if(!x.empty())joined<<x<<'\n';res.qasm=joined.str();return res;
}
static void print_qpc_report(const std::string& source,const std::string& qasm) {
    const auto r=qpc_analyze(qasm);const auto gates=r.quantumStatements>=r.measurements?r.quantumStatements-r.measurements:0;
    std::cout<<"DRNC-Q QPC RESOURCE REPORT\nSource: "<<source<<"\nIR: OpenQASM 3.0\n";
    std::cout<<"Logical qubits declared: "<<r.qubits<<"\nClassical bits declared: "<<r.classicalBits<<"\nQuantum-operation statements: "<<r.quantumStatements<<"\nNon-measurement operation statements: "<<gates<<"\nMeasurement statements: "<<r.measurements<<"\nStatements touching >=2 distinct qubits: "<<r.twoOrMoreQubitOps<<"\nClassical if-statements: "<<r.conditions<<"\nCustom gate definitions: "<<r.customGateDefinitions<<"\nSource-ordered serialized depth estimate: "<<r.estimatedSerialDepth<<"\nQASM size: "<<qasm.size()<<" bytes\n";
    std::cout<<"Operation statement histogram:\n";for(const auto&kv:r.operationKinds)std::cout<<"  "<<kv.first<<": "<<kv.second<<"\n";
    std::cout<<"Note: this is a static IR estimate, not a hardware-native depth, duration, fidelity, or execution result.\n";
}
static int qpc_run_qasm_driver(const std::string& qasm,const std::string& driver,int shots){
    if(qasm.rfind("OPENQASM 3.0;",0)!=0)throw std::runtime_error("QPC driver input must begin with 'OPENQASM 3.0;'" );
    if(shots<1||shots>1000000)throw std::runtime_error("shots must be in 1..1000000");
    if(driver.empty()||access(driver.c_str(),X_OK)!=0)throw std::runtime_error("local QPU driver is missing or not executable: '"+driver+"'");
    char temp[]="/tmp/drncq-qpc-XXXXXX";int fd=mkstemp(temp);if(fd<0)throw std::runtime_error("cannot create temporary QPC circuit file");
    std::size_t off=0;while(off<qasm.size()){ssize_t n=write(fd,qasm.data()+off,qasm.size()-off);if(n<0){int e=errno;close(fd);unlink(temp);throw std::runtime_error("cannot write QPC circuit: "+std::string(std::strerror(e)));}off+=static_cast<std::size_t>(n);}if(close(fd)!=0){unlink(temp);throw std::runtime_error("failed to close QPC circuit file");}
    pid_t child=fork();if(child<0){unlink(temp);throw std::runtime_error("cannot start local QPU driver");}
    if(child==0){std::string shotsText=std::to_string(shots);char* const argv[]={const_cast<char*>(driver.c_str()),const_cast<char*>("--drncq-qpc-driver-v1"),const_cast<char*>("--qasm"),temp,const_cast<char*>("--shots"),const_cast<char*>(shotsText.c_str()),nullptr};execv(driver.c_str(),argv);std::cerr<<"DRNC-Q: could not execute local driver: "<<std::strerror(errno)<<"\n";_exit(127);}
    int status=0;while(waitpid(child,&status,0)<0){if(errno==EINTR)continue;int e=errno;unlink(temp);throw std::runtime_error("failed waiting for QPU driver: "+std::string(std::strerror(e)));}unlink(temp);
    if(WIFEXITED(status)) return WEXITSTATUS(status);
    if(WIFSIGNALED(status)) throw std::runtime_error("local QPU driver terminated by signal "+std::to_string(WTERMSIG(status)));
    throw std::runtime_error("local QPU driver ended with unknown status");
}
static int qpc_run_driver(const std::string& source,const std::string& driver,int shots,bool optimize=false){
    if(target_for_file(source)!="QPC")throw std::runtime_error("qpc-run requires %set[QPC] in the source file");
    std::string qasm=qpc_compile_file(source);
    if(optimize){const auto opt=qpc_optimize(qasm);qasm=opt.qasm;std::cerr<<"DRNC-Q optimizer: canceled "<<opt.canceledPairs<<" involution pair(s), removed "<<opt.removedIdentityRotations<<" identity rotation(s)\n";}
    return qpc_run_qasm_driver(qasm,driver,shots);
}
static void usage(){std::cout<<"DRNC-Q v1.8 — independent PC/QPC-targeted DRNC language\n\nUsage:\n  drnq run PROGRAM.drq                        PC: run VM; QPC: requires local hardware driver\n  drnq check PROGRAM.drq                      validate target-specific compilation\n  drnq ir PROGRAM.drq                         show PC IR or QPC OpenQASM 3 output\n  drnq compile PROGRAM.drq -o FILE            produce DRNQB7 PC bytecode\n  drnq exec FILE                              execute DRNQB7 bytecode on PC VM\n  drnq qpc-compile PROGRAM.drq -o FILE.qasm   compile %set[QPC] to OpenQASM 3\n  drnq qpc-check PROGRAM.drq                  validate QPC target and report compiled circuit\n  drnq qpc-report PROGRAM.drq                 report static circuit resources and operation counts\n  drnq qpc-optimize PROGRAM.drq -o FILE.qasm  conservative semantics-preserving peephole pass\n  drnq qpc-run PROGRAM.drq --driver PATH --shots N [--optimize]\n                                              invoke a local hardware-specific driver\n  drnq qpc-run-qasm FILE.qasm --driver PATH --shots N [--optimize]\n                                              run an existing OpenQASM 3 file through the local driver ABI\n  drnq --version | --about | --help\n\nSource target directive (first non-comment code line):\n  %set[PC]   native host CPU + DRNC-Q local state-vector simulator\n  %set[QPC]  QPU-target compiler; generates hardware circuit IR; does not silently simulate\n\nQPC lowers DRNC constructors, gates, measurement, conditional feed-forward, controlled/adjoint/power gates, custom unitary operations and modular phase rules. OpenQASM 3 output is an IR; physical execution requires a compatible local QPU driver/control stack. No vendor/cloud API is bundled.\n";}
} // namespace drncq

int main(int argc,char**argv){using namespace drncq;try{
    if(argc==2&&std::string(argv[1])=="--version"){std::cout<<"DRNC-Q v1.8 Full Edition (DRNQB7 PC VM + QPC analysis/optimizer + OpenQASM 3 backend)\n";return 0;}
    if(argc==2&&std::string(argv[1])=="--about"){VM::print_about();return 0;}
    if(argc==2&&(std::string(argv[1])=="--help"||std::string(argv[1])=="-h")){usage();return 0;}
    if(argc==3&&std::string(argv[1])=="run"){
        const std::string target=target_for_file(argv[2]);
        if(target=="QPC"){
            const char* drv=std::getenv("DRNC_QPC_DRIVER");
            if(drv&&*drv){int rc=qpc_run_driver(argv[2],drv,1024);if(rc!=0)throw std::runtime_error("local QPU driver failed with exit code "+std::to_string(rc));return 0;}
            throw std::runtime_error("%set[QPC] selects physical-processor execution. No local hardware driver is configured; no quantum job was run. Use qpc-compile to generate the QPC circuit, or qpc-run --driver PATH after installing a driver for your physical QPU.");
        }
        auto p=compile_file(argv[2]);VM vm;vm.execute(p);return 0;
    }
    if(argc==3&&std::string(argv[1])=="check"){
        if(target_for_file(argv[2])=="QPC"){auto q=qpc_compile_file(argv[2]);std::cout<<"PASS: QPC source lowered to OpenQASM 3; ";std::size_t pos=0;unsigned count=0;while((pos=q.find(";",pos))!=std::string::npos){++count;++pos;}std::cout<<count<<" semicolon-terminated instructions/statements; no hardware execution performed\n";return 0;}
        auto p=compile_file(argv[2]);std::cout<<"PASS: "<<argv[2]<<" -> DRNQ-IR; "<<p.code.size()<<" instructions; no execution performed\n";return 0;
    }
    if(argc==3&&std::string(argv[1])=="ir"){
        if(target_for_file(argv[2])=="QPC"){std::cout<<qpc_compile_file(argv[2]);return 0;}auto p=compile_file(argv[2]);print_ir(p);return 0;
    }
    if(argc==5&&std::string(argv[1])=="compile"&&std::string(argv[3])=="-o"){
        if(target_for_file(argv[2])=="QPC") throw std::runtime_error("source uses %set[QPC]; use qpc-compile to produce OpenQASM 3, not PC bytecode");
        auto p=compile_file(argv[2]);auto b=encode(p);write_bin(argv[4],b);std::cout<<"compiled "<<argv[2]<<" -> "<<argv[4]<<" (DRNQB7, "<<p.code.size()<<" instructions, "<<b.size()<<" bytes)\n";return 0;
    }
    if(argc==5&&std::string(argv[1])=="qpc-compile"&&std::string(argv[3])=="-o"){const auto q=qpc_compile_file(argv[2]);write_text_file(argv[4],q);std::cout<<"compiled "<<argv[2]<<" -> "<<argv[4]<<" (OpenQASM 3.0 QPC target; hardware submission not performed)\n";return 0;}
    if(argc==3&&std::string(argv[1])=="qpc-check"){const auto q=qpc_compile_file(argv[2]);std::cout<<"PASS: QPC target lowered to OpenQASM 3.0 ("<<q.size()<<" bytes). Hardware execution not performed.\n";return 0;}
    if(argc==3&&std::string(argv[1])=="qpc-report"){const auto q=qpc_compile_file(argv[2]);print_qpc_report(argv[2],q);return 0;}
    if(argc==5&&std::string(argv[1])=="qpc-optimize"&&std::string(argv[3])=="-o"){const auto original=qpc_compile_file(argv[2]);const auto opt=qpc_optimize(original);write_text_file(argv[4],opt.qasm);std::cout<<"optimized "<<argv[2]<<" -> "<<argv[4]<<" (removed "<<opt.removedIdentityRotations<<" identity rotation(s), canceled "<<opt.canceledPairs<<" adjacent involution pair(s); physical execution not performed)\n";return 0;}
    if(argc>=5&&std::string(argv[1])=="qpc-run"){
        std::string driver;int shots=1024;bool optimize=false;for(int i=3;i<argc;++i){std::string arg=argv[i];if(arg=="--driver"&&i+1<argc)driver=argv[++i];else if(arg=="--shots"&&i+1<argc){char*end=nullptr;long n=std::strtol(argv[++i],&end,10);if(!end||*end||n<1||n>1000000)throw std::runtime_error("--shots must be an integer in 1..1000000");shots=static_cast<int>(n);}else if(arg=="--optimize")optimize=true;else throw std::runtime_error("unknown qpc-run option '"+arg+"'");}if(driver.empty())throw std::runtime_error("qpc-run requires --driver PATH to a local hardware-specific QPU driver");int rc=qpc_run_driver(argv[2],driver,shots,optimize);if(rc!=0)throw std::runtime_error("local QPU driver failed with exit code "+std::to_string(rc));return 0;
    }
    if(argc>=5&&std::string(argv[1])=="qpc-run-qasm"){
        std::string driver;int shots=1024;bool optimize=false;for(int i=3;i<argc;++i){std::string arg=argv[i];if(arg=="--driver"&&i+1<argc)driver=argv[++i];else if(arg=="--shots"&&i+1<argc){char*end=nullptr;long n=std::strtol(argv[++i],&end,10);if(!end||*end||n<1||n>1000000)throw std::runtime_error("--shots must be an integer in 1..1000000");shots=static_cast<int>(n);}else if(arg=="--optimize")optimize=true;else throw std::runtime_error("unknown qpc-run-qasm option '"+arg+"'");}if(driver.empty())throw std::runtime_error("qpc-run-qasm requires --driver PATH to a local hardware-specific QPU driver");std::string qasm=read_text(argv[2]);if(optimize){const auto opt=qpc_optimize(qasm);qasm=opt.qasm;std::cerr<<"DRNC-Q optimizer: canceled "<<opt.canceledPairs<<" involution pair(s), removed "<<opt.removedIdentityRotations<<" identity rotation(s)\n";}int rc=qpc_run_qasm_driver(qasm,driver,shots);if(rc!=0)throw std::runtime_error("local QPU driver failed with exit code "+std::to_string(rc));return 0;
    }
    if(argc==3&&std::string(argv[1])=="exec"){auto p=decode(argv[2]);VM vm;vm.execute(p);return 0;}
    usage();return 2;
}catch(const std::exception&e){std::cerr<<"DRNC-Q error: "<<e.what()<<"\n";return 1;}}

