#include <iostream>
#include <string>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/write.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <arpa/inet.h>
 
using namespace std;
using boost::asio::ip::tcp;

#define SERVER 1
#define CLIENT 2

boost::asio::io_context global_io_context;

class session
  : public enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
  : server_(global_io_context),
    resolver_(global_io_context),
    socket_(move(socket)), 
    acceptor_(global_io_context, {tcp::v4(), 0})
  {}

  void start()
  {
    socket_.read_some(boost::asio::buffer(request));
    parse_request();
  }

private:
  void parse_request()
  {
      /*
        Type 1: CONNECT
              +----+----+----+----+----+----+----+----+----+----+....+----+
              | VN | CD | DSTPORT |       DSTIP       |   USERID     |NULL|
              +----+----+----+----+----+----+----+----+----+----+....+----+
        bytes: 1     1       2              4            variable      1
        Type 2: BIND (SOCKS 4A)
              +----+----+----+----+----+----+----+----+----+----+....+----+----+----+....+----+
              | VN | CD | DSTPORT |  DSTIP(0.0.0.x)   |  USERID      |NULL| DOMAIN NAME  |NULL|
              +----+----+----+----+----+----+----+----+----+----+....+----+----+----+....+----+
        bytes:  1    1       2              4            variable       1     variable      1
      */
      u_char VN = request[0];
      u_char CD = request[1];
      u_int DSTPORT = request[2] << 8 | request[3];
      string DSTIP_str = to_string(request[4]) + "." + to_string(request[5]) + "." + to_string(request[6]) + "." + to_string(request[7]);
      char *USERID = (char *)request + 8;
      char *DOMAIN_NAME;
      // sock4a
      if(request[4] == 0 && request[5] == 0 && request[6] == 0 && request[7] != 0){
        size_t USERID_len = strlen(USERID);
        DOMAIN_NAME = USERID + USERID_len + 1;
        
        string domain_str(DOMAIN_NAME);

        tcp::resolver::query query(domain_str, to_string(DSTPORT));
        tcp::resolver::iterator iterator = resolver_.resolve(query);
        string address;
        while(iterator!=tcp::resolver::iterator())
        {
          boost::asio::ip::address addr=(iterator++)->endpoint().address();
          if(addr.is_v4())
          {
              address = addr.to_string();
              break;
          }
        }
        DSTIP_str = address;
      }
      ifstream fin("./socks.conf");
      u_int is_reply = 91;
      if(fin){
        string permit_str, command, rule;
        while(fin >> permit_str >> command >> rule){
          vector<string> permit_addr_rule;
          boost::split( permit_addr_rule, rule, boost::is_any_of( "." ), boost::token_compress_on );
          vector<string> cur_addr;
          boost::split( cur_addr, DSTIP_str, boost::is_any_of( "." ), boost::token_compress_on );
          if(((command == "c" && CD == 1) || (command == "b" && CD == 2)) && 
             ((permit_addr_rule[0] == "*") || (permit_addr_rule[0] == cur_addr[0])) && 
             ((permit_addr_rule[1] == "*") || (permit_addr_rule[1] == cur_addr[1])) &&
             ((permit_addr_rule[2] == "*") || (permit_addr_rule[2] == cur_addr[2])) &&
             ((permit_addr_rule[3] == "*") || (permit_addr_rule[3] == cur_addr[3]))){
               is_reply = 90;
               break;
          }
        }
      }
      if (VN != 4){
          cerr << "not socks4 request\n";
          is_reply = 91;
      }
      
      cout << "<S_IP>: " << socket_.remote_endpoint().address().to_string() << endl
       << "<S_PORT>: " << socket_.remote_endpoint().port() << endl
       << "<D_IP>: " << DSTIP_str << endl
       << "<D_PORT>: " << DSTPORT << endl;
      if(CD == 1){
        cout << "<Command>: CONNECT\n";
        fflush(stdout);
      }
      else if(CD == 2){
        cout << "<Command>: BIND\n";
        fflush(stdout);
      }
      if(is_reply == 90){
        cout << "<Reply>: Accept\n";
        fflush(stdout);
      }
      else{
        cout << "<Reply>: Reject\n";
        fflush(stdout);
      }
      
      reply[0] = 0;
      reply[1] = is_reply;
      
      if(is_reply == 91){
        for(int i=2; i<8; i++){
          reply[i] = request[i];
        }
        boost::asio::write(socket_, boost::asio::buffer(reply, 8));
        socket_.close();
        exit(0);
      }
      if(CD == 1){
        for(int i=2; i<8; i++){
          reply[i] = 0;
        }

        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(reply, 8),
        [this, self](boost::system::error_code ec, size_t /*length*/)
        {
          if (!ec){
            
          }
          else{
            cerr << ec.message() << endl;
          }
        });

        tcp::resolver::query query(DSTIP_str, to_string(DSTPORT));
        server_.async_connect((resolver_.resolve(query))->endpoint(),
          [this, self, DSTIP_str, DSTPORT](boost::system::error_code ec) {
              cerr << "async_connect\n";
              if (ec) {
                  cerr << "Error:  async_connect() " + DSTIP_str + ":" + to_string(DSTPORT) + " " + ec.message()  << endl;
                  return;
              }
              do_read(CLIENT);
              do_read(SERVER);
          });
        // server_.connect((resolver_.resolve(query))->endpoint());
        
        
        // boost::asio::write(socket_, boost::asio::buffer(reply, 8));        
      }
      else if(CD == 2){
        cerr << "port=" << acceptor_.local_endpoint().port() << endl;
        reply[2] = acceptor_.local_endpoint().port() / 256;
        reply[3] = acceptor_.local_endpoint().port() % 256;
        for(size_t i=4; i<8; i++){
          reply[i] = 0;
        }
        boost::asio::write(socket_, boost::asio::buffer(reply, 8));

        auto self(shared_from_this());
        acceptor_.async_accept(
        [this, self](boost::system::error_code ec, tcp::socket new_socket)
        {
          if(!ec){
            server_ = move(new_socket);
            auto self(shared_from_this());
            boost::asio::async_write(socket_, boost::asio::buffer(reply, 8), 
            [this, self](boost::system::error_code ec, size_t /*length*/)
            {
              if (!ec){
                do_read(CLIENT);
                do_read(SERVER);
              }
              else{
                cerr << "async_write error: " << ec.message() << endl;
              }
            });
          }
          else{
            cerr << "accept error: " << ec.message() << endl;
          }
        });
        
        
      }
      else{
        cerr << "type error: CD =" << CD << endl;
      }
  }
  
  void do_read(int target){
    auto self(shared_from_this());
    if(target == SERVER){
      server_.async_read_some(boost::asio::buffer(data_from_server_),
        [this, self](boost::system::error_code ec, size_t length)
        {
          if (!ec){
            do_write(CLIENT, length);
          }
          else{
            cerr << ec.message() << endl;
          }
        });
    }
    else if(target == CLIENT){
      socket_.async_read_some(boost::asio::buffer(data_from_client_),
        [this, self](boost::system::error_code ec, size_t length)
        {
          if (!ec)
            do_write(SERVER, length);
          else{
            cerr << ec.message() << endl;
          }
        });
    }
  }
  void do_write(int target, size_t length){
    auto self(shared_from_this());
    if(target == SERVER){
      boost::asio::async_write(server_, boost::asio::buffer(data_from_client_, length),
        [this, self](boost::system::error_code ec, size_t /*length*/)
        {
          if (!ec)
            do_read(CLIENT);
          else{
            cerr << ec.message() << endl;
          }
        });
    }
    else if(target == CLIENT){
      boost::asio::async_write(socket_, boost::asio::buffer(data_from_server_, length),
        [this, self](boost::system::error_code ec, size_t /*length*/)
        {
          if (!ec)
            do_read(SERVER);
          else{
            cerr << ec.message() << endl;
          }
        });
    }
  }
  
  tcp::socket server_;
  tcp::resolver resolver_;
  tcp::socket socket_;
  tcp::acceptor acceptor_;
  char data_from_client_[1024];
  char data_from_server_[1024];
  u_char request[1024];
  u_char reply[8];
  string S_IP;
  u_int S_PORT;

  char data_[1024];
};

class server
{
public:
  server(unsigned short port)
    : signal_(global_io_context, SIGCHLD),
      acceptor_(global_io_context, {tcp::v4(), port}),
      socket_(global_io_context)
  {
    wait_for_signal();
    accept();
  }

private:
  void wait_for_signal()
  {
    signal_.async_wait(
        [this](boost::system::error_code /*ec*/, int /*signo*/)
        {
          if (acceptor_.is_open())
          {
            int status = 0;
            while (waitpid(-1, &status, WNOHANG) > 0) {}

            wait_for_signal();
          }
        });
  }
  void accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket new_socket)
        {
          if (!ec)
          {
            socket_ = move(new_socket);
            global_io_context.notify_fork(boost::asio::io_context::fork_prepare);

            int child = fork();
            if (child == 0)
            {
              global_io_context.notify_fork(boost::asio::io_context::fork_child);
              acceptor_.close();
              signal_.cancel();
              make_shared<session>(move(socket_))->start();
            }
            else if(child > 0)
            {
              global_io_context.notify_fork(boost::asio::io_context::fork_parent);
              socket_.close();
              accept();
            }
            else{
              cerr << "fork error\n";
              accept();
            }
          }
          else
          {
            cerr << "Accept error: " << ec.message() << endl;
            accept();
          }
        });
  }

  boost::asio::signal_set signal_;
  tcp::acceptor acceptor_;
  tcp::socket socket_;
};

int main(int argc, char* argv[])
{
    int null_fd = open("/dev/null", O_WRONLY);
    dup2(null_fd, STDERR_FILENO);
    try
    {
        if (argc < 2)
        {
            cerr << "Usage: ./socks_server [port]\n";
            return 1;
        }

        server s(atoi(argv[1]));
        global_io_context.run();
    }
    catch (exception& e)
    {
      cerr << "Exception: " << e.what() << "\n";
      exit(0);
    }
    return 0;
}