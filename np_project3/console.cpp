#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <string>
#include <vector>
#include <fstream> 
#include <unistd.h>
#include <cstdlib>

using namespace std;
using boost::asio::ip::tcp;

struct sess
{
    string host;
    string port;
    string file;
};

vector<sess> allSession;
size_t endSession = 0;

void encode_html(string& data) {
    // cerr << data << "=====> ";
    boost::algorithm::replace_all(data, "&", "&amp;");
    boost::algorithm::replace_all(data, "\"", "&quot;");
    boost::algorithm::replace_all(data, "\'", "&apos;");
    boost::algorithm::replace_all(data, "\r", "");
    boost::algorithm::replace_all(data, "<", "&lt;");
    boost::algorithm::replace_all(data, ">", "&gt;");
    boost::algorithm::replace_all(data, "\n", "&NewLine;");
}

void print_deb_msg(string content){
    return;
    // encode_html(content);
    // cout << "<script>document.getElementById('dbg').innerHTML += '" << content << "';</script>\n";
    // fflush(stdout);
}

void print_shell(string session, string content){
    encode_html(content);
    cout << "<script>document.getElementById('" << session << "').innerHTML += '" << content << "';</script>\n";
    fflush(stdout);
}

void print_command(string session, string content){
    encode_html(content);
    // cerr << "print_command " << content << endl;
    cout << "<script>document.getElementById('" << session << "').innerHTML += '<b>" << content << "</b>';</script>\n";
    fflush(stdout);
}

void parse_QUERY_STRING(){
    // "h0=nplinux2.cs.nctu.edu.tw&p0=4566&f0=t1.txt""&h1=&p1=&f1="&h2=&p2=&f2=&h3=&p3=&f3=&h4=&p4=&f4=
    string query(getenv("QUERY_STRING"));
    // cerr << query << endl;
    vector<string> rs;
    boost::split( rs, query, boost::is_any_of( "=&" ), boost::token_compress_on );
    for( vector<string>::iterator it = rs.begin(); it != rs.end();){
        if((*it).length() <= 2){
            it++;
            continue;
        }
        sess new_sess;
        new_sess.host = *it;
        new_sess.port = *(it+2);
        new_sess.file = *(it+4);
        allSession.push_back(new_sess);
        it+=5;
    }
        
}

void print_th(){
    for(size_t i=0; i<allSession.size(); i++){
        cout << "                <th scope=\"col\">" << allSession[i].host << ":" << allSession[i].port << "</th>\n";
    }
}

void print_td(){
    for(size_t i=0; i<allSession.size(); i++){
        cout << "                <td><pre id=\"s" << i << "\" class=\"mb-0\"></pre></td>\n";
    }
}

void print_html(){
    cout << "Content-type: text/html\r\n\r\n";
    // raw string literals
    cout << R"html(<!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8" />
        <title>NP Project 3 Console</title>
        <link
        rel="stylesheet"
        href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
        integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
        crossorigin="anonymous"
        />
        <link
        href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
        rel="stylesheet"
        />
        <link
        rel="icon"
        type="image/png"
        href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
        />
        <style>
        * {
            font-family: 'Source Code Pro', monospace;
            font-size: 1rem !important;
        }
        body {
            background-color: #212529;
        }
        pre {
            color: #cccccc;
        }
        b {
            color: #01b468;
        }
        </style>
    </head>
    <body>
        <div id="dbg" style="display:none"></div>
        <table class="table table-dark table-bordered">
        <thead>
            <tr>)html" << endl;
                print_th();
cout << R"html(            </tr>
        </thead>
        <tbody>
            <tr>)html" << endl;
                print_td();
cout << R"html(            </tr>
        </tbody>
        </table>
    </body>
    </html>)html" << endl;
}

class client : public enable_shared_from_this<client>
{
public:
    client(boost::asio::io_service& io_service, int id, 
        const std::string& server, const std::string& port, const std::string& file)
    : resolver_(io_service),
        socket_(io_service), 
        host_(server), 
        port_(port)
    {
        string path = "./test_case/" + file;
        input_file.open(path.c_str());
        if(!input_file){
            print_deb_msg(id_ + " Error code:" + strerror(errno) + " ;");   
        }

        id_ = "s" + to_string(id);
    }
    void start(){
        auto self(shared_from_this());
        tcp::resolver::query query(host_, port_);
        resolver_.async_resolve(query, 
            [this, self](const boost::system::error_code& ec, tcp::resolver::iterator iterator){
                // cerr << "async_resolve\n";
                print_deb_msg(id_ + " async_resolve;");
                handle_resolve(ec, iterator);
            }); 
    }
private:
    void handle_resolve(const boost::system::error_code& err,
        tcp::resolver::iterator iterator)
    {
        if (!err)
        {
            auto self(shared_from_this());
            socket_.async_connect(
                iterator->endpoint(),
                [this, self](boost::system::error_code ec) {
                    // cerr << "async_connect\n";
                    print_deb_msg(id_ + " async_connect;");
                    if (ec) {
                        cerr << "Error:  async_connect() " + ec.message();
                        return;
                    }
                    do_read();
                });
        }
        else
        {
            cerr << "Error: " << err.message() << "\n";
        }
    }
    void do_read()
    {
        if(Isexit == 1){
            return;
        }
        print_deb_msg(id_ + " do_read;");
        // cerr << "do_read\n";
        auto self(shared_from_this());
        socket_.async_read_some(
            boost::asio::buffer(_data, max_length),
            [this, self](boost::system::error_code ec, size_t length) {
                if (!ec) {
                    string str(_data.begin(), _data.begin() + length);
                    print_shell(id_, str);
                    if (str.find("% ") != string::npos) {
                        do_write();
                    }
                    do_read();
                }
            });
    }

    void do_write()
    {
        if(Isexit == 1){
            return;
        }
        print_deb_msg(id_ + " do_write;");
        // cerr << "do_write\n";
        auto self(shared_from_this());
        string cmd_str;
        if (getline(input_file, cmd_str)) {
            cmd_str += "\n";
            print_command(id_, cmd_str);
            print_deb_msg(cmd_str + ";");
            socket_.async_send(
                boost::asio::buffer(cmd_str, cmd_str.size()),
                [this, self, cmd_str](boost::system::error_code ec, size_t length) {
                    // cerr << "after send\n";
                    print_deb_msg(id_ + " after send;");
                    if (!ec) {
                        if(cmd_str == "exit\n"){
                            // cerr << "exit\n";
                            print_deb_msg(id_ + " exit;");
                            do_exit();
                        }
                    }
                });
        }
        else{
            print_deb_msg(id_ + " getline error;");
        }
    }

    void do_exit()
    {
        Isexit = 1;
        endSession++;
        socket_.close();
        if(endSession == allSession.size())
            exit(0);
    }
    
    tcp::resolver resolver_;
    tcp::socket socket_;
    enum { max_length = 1024 };
    array<u_char, max_length> _data;
    string id_;
    ifstream  input_file;
    string host_;
    string port_;
    int Isexit = 0;
};

int main(int argc, char* argv[]){
    
    parse_QUERY_STRING();
    print_html();

    boost::asio::io_service io_service;

    for(size_t i = 0; i<allSession.size(); i++){
        shared_ptr<client> c = make_shared<client>(io_service, i, allSession[i].host, allSession[i].port, allSession[i].file);
        c->start();
    }
    io_service.run();
    return 0;
}