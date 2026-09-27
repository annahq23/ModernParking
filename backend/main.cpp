#include "httplib.h"
#include <sqlite3.h>
#include <iostream>
#include <string>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace std;
using namespace httplib;

sqlite3* db = nullptr;
const int TOTAL_SLOTS = 10;
const int FREE_MINUTES = 30;
const double RATE_PER_HOUR = 100.0;

string nowText(time_t t) {
    tm* local = localtime(&t);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local);
    return buffer;
}
string normalize(string s) {
    s.erase(remove_if(s.begin(), s.end(), [](unsigned char c){return isspace(c);}), s.end());
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return toupper(c);});
    return s;
}
bool execSql(const string& sql) {
    char* err=nullptr;
    int rc=sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){cerr<<(err?err:"SQL error")<<endl;sqlite3_free(err);return false;}
    return true;
}
void initDb() {
    if(sqlite3_open("parking.db",&db)!=SQLITE_OK) throw runtime_error("Cannot open database");
    execSql("PRAGMA foreign_keys=ON;");
    execSql("CREATE TABLE IF NOT EXISTS slots(id INTEGER PRIMARY KEY, occupied INTEGER NOT NULL DEFAULT 0);");
    execSql("CREATE TABLE IF NOT EXISTS visits(id INTEGER PRIMARY KEY AUTOINCREMENT, registration TEXT NOT NULL, slot_id INTEGER NOT NULL, entry_time INTEGER NOT NULL, exit_time INTEGER, fee REAL, status TEXT NOT NULL DEFAULT 'ACTIVE', FOREIGN KEY(slot_id) REFERENCES slots(id));");
    execSql("CREATE TABLE IF NOT EXISTS payments(id INTEGER PRIMARY KEY AUTOINCREMENT, visit_id INTEGER NOT NULL, amount REAL NOT NULL, amount_due REAL NOT NULL, paid_at INTEGER NOT NULL, status TEXT NOT NULL, FOREIGN KEY(visit_id) REFERENCES visits(id));");
    for(int i=1;i<=TOTAL_SLOTS;i++) execSql("INSERT OR IGNORE INTO slots(id,occupied) VALUES("+to_string(i)+",0);");
}
bool getActive(const string& reg,int& visitId,int& slot,time_t& entry) {
    sqlite3_stmt* st=nullptr;
    sqlite3_prepare_v2(db,"SELECT id,slot_id,entry_time FROM visits WHERE registration=? AND status='ACTIVE' LIMIT 1",-1,&st,nullptr);
    sqlite3_bind_text(st,1,reg.c_str(),-1,SQLITE_TRANSIENT);
    bool found=sqlite3_step(st)==SQLITE_ROW;
    if(found){visitId=sqlite3_column_int(st,0);slot=sqlite3_column_int(st,1);entry=(time_t)sqlite3_column_int64(st,2);}
    sqlite3_finalize(st);return found;
}
int availableSlot() {
    sqlite3_stmt* st=nullptr;int id=-1;
    sqlite3_prepare_v2(db,"SELECT id FROM slots WHERE occupied=0 ORDER BY id LIMIT 1",-1,&st,nullptr);
    if(sqlite3_step(st)==SQLITE_ROW) id=sqlite3_column_int(st,0);
    sqlite3_finalize(st);return id;
}
double feeFor(int minutes) {
    if(minutes<=FREE_MINUTES)return 0;
    return ceil((minutes-FREE_MINUTES)/60.0)*RATE_PER_HOUR;
}
string jsonEscape(const string& s) {
    string out;for(char c:s){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else out+=c;}return out;
}
string bodyValue(const string& body,const string& key) {
    // Simple extraction for the small JSON payloads used by this demo.
    string needle="\""+key+"\"";auto p=body.find(needle);if(p==string::npos)return "";
    p=body.find(':',p+needle.size());if(p==string::npos)return "";
    p=body.find('"',p);if(p==string::npos)return "";
    auto q=body.find('"',p+1);if(q==string::npos)return "";
    return body.substr(p+1,q-p-1);
}
double bodyNumber(const string& body,const string& key) {
    string needle="\""+key+"\"";auto p=body.find(needle);if(p==string::npos)return 0;
    p=body.find(':',p+needle.size());if(p==string::npos)return 0;
    return atof(body.c_str()+p+1);
}
void error(Response& res,int code,const string& msg){res.status=code;res.set_content("{\"error\":\""+jsonEscape(msg)+"\"}","application/json");}

int main() {
    try { initDb(); } catch(const exception& e){cerr<<e.what()<<endl;return 1;}
    Server svr;
    svr.Get("/",[](const Request&,Response& res){res.set_file_content("frontend/index.html","text/html");});
    svr.Get("/style.css",[](const Request&,Response& res){res.set_file_content("frontend/style.css","text/css");});
    svr.Get("/app.js",[](const Request&,Response& res){res.set_file_content("frontend/app.js","application/javascript");});
    svr.Get("/api/status",[](const Request&,Response& res){
        string out="{\"total\":"+to_string(TOTAL_SLOTS)+",\"available\":";
        sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM slots WHERE occupied=0",-1,&st,nullptr);
        int avail=0;if(sqlite3_step(st)==SQLITE_ROW)avail=sqlite3_column_int(st,0);sqlite3_finalize(st);
        out+=to_string(avail)+",\"slots\":[";
        sqlite3_prepare_v2(db,"SELECT id,occupied FROM slots ORDER BY id",-1,&st,nullptr);
        bool first=true;while(sqlite3_step(st)==SQLITE_ROW){if(!first)out+=",";first=false;out+="{\"id\":"+to_string(sqlite3_column_int(st,0))+",\"occupied\":"+(sqlite3_column_int(st,1)?"true":"false")+"}";}
        sqlite3_finalize(st);out+="],\"vehicles\":[";
        sqlite3_prepare_v2(db,"SELECT registration,slot_id,entry_time FROM visits WHERE status='ACTIVE' ORDER BY id",-1,&st,nullptr);
        first=true;while(sqlite3_step(st)==SQLITE_ROW){if(!first)out+=",";first=false;out+="{\"registration\":\""+jsonEscape((const char*)sqlite3_column_text(st,0))+"\",\"slot\":"+to_string(sqlite3_column_int(st,1))+",\"entry\":\""+nowText((time_t)sqlite3_column_int64(st,2))+"\"}";}
        sqlite3_finalize(st);out+="]}";res.set_content(out,"application/json");
    });
    svr.Post("/api/entry",[](const Request& req,Response& res){
        string reg=normalize(bodyValue(req.body,"registration"));
        if(reg.empty()){error(res,400,"Enter a registration number.");return;}
        int id,slot;time_t entry;
        if(getActive(reg,id,slot,entry)){error(res,409,"Vehicle is already parked.");return;}
        int s=availableSlot();if(s<0){error(res,409,"Parking is full.");return;}
        time_t now=time(nullptr);
        execSql("BEGIN;");
        execSql("UPDATE slots SET occupied=1 WHERE id="+to_string(s)+";");
        sqlite3_stmt* st=nullptr;sqlite3_prepare_v2(db,"INSERT INTO visits(registration,slot_id,entry_time,status) VALUES(?,?,?,'ACTIVE')",-1,&st,nullptr);
        sqlite3_bind_text(st,1,reg.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(st,2,s);sqlite3_bind_int64(st,3,(sqlite3_int64)now);
        int ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);execSql(ok?"COMMIT;":"ROLLBACK;");
        if(!ok){error(res,500,"Could not save vehicle.");return;}
        res.set_content("{\"slot\":"+to_string(s)+",\"entry\":\""+nowText(now)+"\"}","application/json");
    });
    svr.Post("/api/quote",[](const Request& req,Response& res){
        string reg=normalize(bodyValue(req.body,"registration"));int id,slot;time_t entry;
        if(!getActive(reg,id,slot,entry)){error(res,404,"Vehicle not found.");return;}
        time_t now=time(nullptr);int mins=(int)ceil(difftime(now,entry)/60.0);double fee=feeFor(mins);
        res.set_content("{\"slot\":"+to_string(slot)+",\"entry\":\""+nowText(entry)+"\",\"exit\":\""+nowText(now)+"\",\"minutes\":"+to_string(mins)+",\"fee\":"+to_string(fee)+"}","application/json");
    });
    svr.Post("/api/payment",[](const Request& req,Response& res){
        string reg=normalize(bodyValue(req.body,"registration"));double amount=bodyNumber(req.body,"amount");
        int id,slot;time_t entry;if(!getActive(reg,id,slot,entry)){error(res,404,"Vehicle not found.");return;}
        time_t now=time(nullptr);int mins=(int)ceil(difftime(now,entry)/60.0);double fee=feeFor(mins);
        if(amount+0.00001<fee){error(res,400,"Insufficient payment. Amount due is KSh "+to_string(fee));return;}
        execSql("BEGIN;");
        sqlite3_stmt* st=nullptr;
        sqlite3_prepare_v2(db,"INSERT INTO payments(visit_id,amount,amount_due,paid_at,status) VALUES(?,?,?,?, 'SUCCESS')",-1,&st,nullptr);
        sqlite3_bind_int(st,1,id);sqlite3_bind_double(st,2,amount);sqlite3_bind_double(st,3,fee);sqlite3_bind_int64(st,4,(sqlite3_int64)now);
        bool ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);
        if(ok){sqlite3_prepare_v2(db,"UPDATE visits SET exit_time=?,fee=?,status='COMPLETED' WHERE id=?",-1,&st,nullptr);sqlite3_bind_int64(st,1,(sqlite3_int64)now);sqlite3_bind_double(st,2,fee);sqlite3_bind_int(st,3,id);ok=sqlite3_step(st)==SQLITE_DONE;sqlite3_finalize(st);}
        if(ok)ok=execSql("UPDATE slots SET occupied=0 WHERE id="+to_string(slot)+";");
        execSql(ok?"COMMIT;":"ROLLBACK;");
        if(!ok){error(res,500,"Payment could not be recorded.");return;}
        res.set_content("{\"change\":"+to_string(amount-fee)+",\"barrier\":\"OPEN\"}","application/json");
    });
    cout<<"Parking website running at http://localhost:8080\n";
    cout<<"Keep this window open while using the website.\n";
    svr.listen("0.0.0.0",8080);
    sqlite3_close(db);
}
