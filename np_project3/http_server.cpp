#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

#define	REQUEST_METHOD 0
#define REQUEST_URI 1
#define QUERY_STRING 2
#define SERVER_PROTOCOL 3 
#define HTTP_HOST 4
#define SERVER_ADDR 5
#define SERVER_PORT 6
#define REMOTE_ADDR 7
#define REMOTE_PORT 8

using boost::asio::ip::tcp;
using namespace std;

class session
  : public enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(move(socket))
  {
    string ClientIp = socket_.remote_endpoint().address().to_string();
    env_var[REMOTE_ADDR] = ClientIp;
    unsigned short ClientPort = socket_.remote_endpoint().port();
    env_var[REMOTE_PORT] = to_string(ClientPort);
    
    string ServerIp = socket_.local_endpoint().address().to_string();
    env_var[SERVER_ADDR] = ServerIp;
    unsigned short ServerPort = socket_.local_endpoint().port();
    env_var[SERVER_PORT] = to_string(ServerPort);
  }

  void start()
  {
    // cout << "start\n";
    do_read();
  }

private:
  void do_read()
  {
    // cout << "do_read\n";
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, size_t length)
        {
          if (!ec)
          {
            // parse
            parse_request(length);

            // fork
            pid_t newChild;
            if( (newChild = fork()) == -1) {
              perror("fork error");
            }
            else if (newChild == 0) {
              /* child process */
              // setenv
              for(int i = 0; i<9; i++){
                setenv(env[i], env_var[i].c_str(), 1);
              }

              // dup
              int sock = socket_.native_handle();
              // dup2(sock, STDERR_FILENO);
              dup2(sock, STDIN_FILENO);
              dup2(sock, STDOUT_FILENO);

              socket_.close();

              // exec
              long unsigned int len = env_var[REQUEST_URI].length();
              long unsigned int start = 0;
              for(long unsigned int i = 0; i<env_var[REQUEST_URI].length(); i++){
                if(env_var[REQUEST_URI][i] == '/'){
                  start = i+1;
                }
                if(env_var[REQUEST_URI][i] == '?'){
                  len = i-1;
                  break;
                }
              }
              string EXEC_FILE = "./" + env_var[REQUEST_URI].substr(start, len);

              cerr << "EXEC_FILE = " << EXEC_FILE << endl;
              
              cout << "HTTP/1.1 200 OK\r\n";
              if (execlp(EXEC_FILE.c_str(), EXEC_FILE.c_str(), NULL) < 0) {
                perror("execlp");
                fflush(stdout);
              }
            }
            else{
              /* parent process */
              socket_.close();
            }
          }
        });
  }

  void parse_request(size_t length)
  {
// GET /panel.cgi?a=b&c=d HTTP/1.1
// Host: nplinux10.cs.nctu.edu.tw:4623
// Connection: keep-alive
// Upgrade-Insecure-Requests: 1
// User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/96.0.4664.45 Safari/537.36
// Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9
// Accept-Encoding: gzip, deflate
// Accept-Language: zh-TW,zh;q=0.9,en-US;q=0.8,en;q=0.7
// Cookie: _ga=GA1.3.1864749137.1619774950; _gid=GA1.3.821723887.1638031516
    long unsigned int last=0;
    long unsigned int i=0;
    int env_i = 0;
    for(; i<length; i++){
      if(data_[i] == ' ' || data_[i] == '\r'){
        if(data_[i-5] == 'H' && data_[i-4] == 'o' && data_[i-3] == 's' && data_[i-2] == 't'){
          last = i+1;
          continue;
        }
        // char *temp = new char[i-last];
        char temp[1024];
        memset(temp, '\0', 1024);
        for(long unsigned int k=0, j = last; j<i; k++, j++){
          temp[k] = data_[j];
          char query[1024];
          memset(query, '\0', 1024);
          if(env_i == QUERY_STRING && data_[j] == '?'){
            // char *query = new char[i-(j+1)];
            for(long unsigned int q=0, p=j+1; p<i; q++, p++){
              query[q] = data_[p];
            }
            string to_s(query);
            cout << to_s << endl;
            env_var[QUERY_STRING] = to_s;
            env_i = REQUEST_URI;
            // delete[] query;
          }
          
        }
        if(env_i == QUERY_STRING){
          env_var[QUERY_STRING] = "";
          env_i = REQUEST_URI;
        }
        if(env_i == REQUEST_METHOD){
          string to_s(temp);
          env_var[REQUEST_METHOD] = to_s;
          env_i = QUERY_STRING;
        }
        else if(env_i == REQUEST_URI){
          string to_s(temp);
          env_var[REQUEST_URI] = to_s;
          env_i = SERVER_PROTOCOL;
        }
        else if(env_i == SERVER_PROTOCOL){
          string to_s(temp);
          env_var[SERVER_PROTOCOL] = to_s;
          env_i++;
        }
        else if(env_i == HTTP_HOST){
          string to_s(temp);
          env_var[HTTP_HOST] = to_s;
          // env_i++;
          break;
        }
        // delete[] temp;
        last = i+1;
      }
    }
    // for(int c = 0; c<9; c++){
    //   cout << env[c] << " = " << env_var[c] << endl;
    // }
  }
  

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
  string env_var[9];
  char env[9][16] = {"REQUEST_METHOD", "REQUEST_URI", "QUERY_STRING", "SERVER_PROTOCOL", 
                      "HTTP_HOST", "SERVER_ADDR", "SERVER_PORT", "REMOTE_ADDR", "REMOTE_PORT"};
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    int one = 1;
    setsockopt(acceptor_.native_handle(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &one, sizeof(one));
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            // cout << "accept!\n";
            make_shared<session>(move(socket))->start();
          }
          do_accept();
        });
  }

  tcp::acceptor acceptor_;
};

void sig_chld(int signo)
{
    pid_t pid;
    int stat;
    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0);
    return;
}

int main(int argc, char* argv[])
{
  signal(SIGCHLD, sig_chld);
  try
  {
    if (argc != 2)
    {
      cerr << "Usage: ./http_server [port]\n";
      return 1;
    }

    boost::asio::io_context io_context;
    server s(io_context, atoi(argv[1]));
    io_context.run();
  }
  catch (exception& e)
  {
    cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}