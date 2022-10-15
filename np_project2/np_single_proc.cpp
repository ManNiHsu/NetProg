#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <cstring>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <array>
#include <sstream>
#include <algorithm>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <arpa/inet.h>

using namespace std;

#define	MAXLINE	15000
#define	QLEN	   30

struct pipeRec
{
    int line;
    int pipe0;
    int pipe1;
    int err;    
};

struct environvar{
    int numOfWait;
    vector<pid_t> childs;
    int fileRed;
    int savedFd;
    int numPipe;
    vector<pipeRec> numberedPipe;
    int line;
    string path;
};

struct userinfo
{
    int id; // different from index; index means the position in vector<userinfo> users
    string addr;
    int port;
    string name;
    int fd;
    environvar env;   
};

struct less_than_id
{
    inline bool operator() (const userinfo& user1, const userinfo& user2)
    {
        return (user1.id < user2.id);
    }
};

vector<userinfo> users;
int userPipeRead[31][31]; //userPipeRead[i][j] means #i send pipe to #j, and store the read end
int msock; // master server socket
fd_set	rfds;		// read file descriptor set
fd_set	afds;		// active file descriptor set
int nfds;

userinfo createNewUser(string addr, int port, int sockfd){
    userinfo newUser;
    newUser.id = users.size()+1;
    for(int i = 0; i<users.size(); i++){
        if(users[i].id != (i+1)){
            newUser.id = (i+1);
            break;
        }
    }
    cout << "new user id =" << newUser.id << endl;
    newUser.addr = addr;
    newUser.port = port;
    newUser.name = "(no name)";
    newUser.fd = sockfd;
    
    newUser.env.numOfWait = 0;
    newUser.env.fileRed = 0;
    newUser.env.savedFd = 0;
    newUser.env.numPipe = 0;
    newUser.env.line = 0;
    newUser.env.path = "bin:.";

    return newUser;
}

int findUserIndexBySockfd(int fd){
    for(int i = 0; i<users.size(); i++){
        if(users[i].fd == fd){
            return(i);
        }
    }
    return(-1);
}
int findUserSockfdById(int id){
    for(int i = 0; i<users.size(); i++){
        if(users[i].id == id){
            return(users[i].fd);
        }
    }
    return(-1);
}
int findUserIndexById(int id){
    for(int i = 0; i<users.size(); i++){
        if(users[i].id == id){
            return i;
        }
    }
    return(-1);
}

int tellto(int sockfd, string s){
    write(sockfd, s.c_str(), s.size());
    return(0);
}

int broacast(string s){
    for (int fd=0; fd<nfds; ++fd){
        if (fd != msock && FD_ISSET(fd, &afds)){
            tellto(fd, s);
        }
    }
    return(0);
}

void sig_pipe(int signo){
    cerr << "pipe error\n";
}

void sig_chld(int signo)
{
    pid_t pid;
    int stat;
    while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0){
        for(int i = 0; i<users.size(); i++){
            vector<pid_t> childs = users[i].env.childs;
            vector<pid_t>::iterator it = find(childs.begin(), childs.end(), pid);
            if (it != childs.end())
                users[i].env.numOfWait++;
        }
    }
    return;
}

int npshell(int userIndex){
    int ret = -1;

    // recovery env
    int stdout_backup = dup(STDOUT_FILENO);
    int stdin_backup = dup(STDIN_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    const char* path_backup = getenv("PATH");
    // cout << stdout_backup << " " << stdin_backup << " " << stderr_backup << endl;

    int fileRed = users[userIndex].env.fileRed;
    int savedFd = users[userIndex].env.savedFd;
    int numPipe = users[userIndex].env.numPipe;
    vector<pipeRec> numberedPipe = users[userIndex].env.numberedPipe;
    dup2(users[userIndex].fd, STDOUT_FILENO);
    dup2(users[userIndex].fd, STDERR_FILENO);
    dup2(users[userIndex].fd, STDIN_FILENO);
    setenv("PATH", (users[userIndex].env.path).c_str(), 1);
    int line = users[userIndex].env.line;

    while(++line){
        int isNumberedPipe = 0;
        string cmd;
        vector<string> input;
        
        vector<int> pipePos;
        pipePos.push_back(0);

        if(fileRed == 1){
            // cerr << savedFd << " recover " << STDOUT_FILENO << endl;
            fileRed = 0;
            dup2(savedFd, STDOUT_FILENO);
            close(savedFd);
        }

        string str;
        // cerr << "% ";
        getline(cin, str);
        if(str == ""){
            line--;
            break;
            // continue;
        }
        stringstream allInput(str);
        pipeRec newNumberedPipe;
        int sendmsg = 0;
        int useUserPipe0 = 0; // use 0(read)
        int useUserPipe1 = 0; // use 1(write)
        int userpipe0 = -1;
        int userpipe[2];
        string recv_msg = "";
        string send_msg = "";
        while(allInput >> cmd){
            if(cmd == "exit" || cmd == "EOF"){
                ret = 0;
                break;
            }
            else if(cmd == "yell" || cmd == "tell"){
                sendmsg = 1;
            }
                
            if(fileRed == 1){
                const char* path = cmd.c_str();
                int fd = open(path, O_RDWR|O_CREAT|O_TRUNC, 0666);//dup target file to STDOUT_FILENO(1)
                dup2(fd, STDOUT_FILENO);
                close(fd);
                break;
            }
            else if(cmd == ">" && sendmsg == 0){
                fileRed = 1;
                savedFd = dup(STDOUT_FILENO); //save STDOUT_FILENO(1)
            }
            else if(cmd == "|" && sendmsg == 0){
                pipePos.push_back(input.size());
            }
            else if((cmd[0] == '|' || cmd[0] == '!') && sendmsg == 0){
                int nline = 0;
                for(int i = 1; i<cmd.length(); i++)
                    nline = nline*10 + (cmd[i] - '0');
                if(cmd[0] == '|')
                    newNumberedPipe = {line+nline, 0, 0, 0};
                else if(cmd[0] == '!')
                    newNumberedPipe = {line+nline, 0, 0, 1};
                isNumberedPipe = 1;
                break;
            }
            // user pipe
            else if(cmd[0] == '>' && sendmsg == 0){
                int senderId = users[userIndex].id;
                string rec;
                rec.assign(cmd, 1, cmd.size()-1);
                int receiverId = stoi(rec);
                int receiverIndex = findUserIndexById(receiverId);
                if(receiverIndex < 0){
                    // *** Error: user #<user_id> does not exist yet. ***
                    cerr << "*** Error: user #" << receiverId << " does not exist yet. ***\n";
                    // ret = 1;
                    // break;
                    useUserPipe1 = -1;
                    continue;
                }
                else if(userPipeRead[senderId][receiverId] > 0){
                    // *** Error: the pipe #<sender_id>->#<receiver_id> already exists. ***
                    cerr << "*** Error: the pipe #" << senderId << "->#" << receiverId << " already exists. ***\n";
                    // ret = 1;
                    // break;
                    useUserPipe1 = -1;
                    continue;
                }
                else{
                    if (pipe(userpipe) < 0)
                        perror("userpipe error");
                        // cerr << "can't create pipes\n";
                    useUserPipe1 = 1;
                    userPipeRead[senderId][receiverId] = userpipe[0];
                    string command = str;
                    command.erase(remove(command.begin(), command.end(), '\n'), command.end());
                    command.erase(remove(command.begin(), command.end(), '\r'), command.end());
                    // *** <sender_name> (#<sender_id>) just piped ’<command>’ to <receiver_name> (#<receiver_id>) ***
                    send_msg = "*** " + users[userIndex].name + " (#" + to_string(users[userIndex].id) + ") just piped '" + command + "' to " + users[receiverIndex].name + " (#" + to_string(receiverId) + ") ***\n";
                    // cout << "*** " << users[userIndex].name << " (#" << to_string(users[userIndex].id) << ") just piped '" << allInput.str() << "' to " + users[receiverIndex].name << " (#" + to_string(receiverId) << ") ***\n";
                    // broacast(msg);
                }   
            }
            else if(cmd[0] == '<' && sendmsg == 0){
                int receiverId = users[userIndex].id;
                string sen;
                sen.assign(cmd, 1, cmd.size()-1);
                int senderId = stoi(sen);
                int senderIndex = findUserIndexById(senderId);
                if(senderIndex < 0){
                    // *** Error: user #<user_id> does not exist yet. ***
                    cerr << "*** Error: user #" << senderId << " does not exist yet. ***\n";
                    // ret = 1;
                    // break;
                    useUserPipe0 = -1;
                    continue;
                }
                else if(userPipeRead[senderId][receiverId] < 0){
                    // *** Error: the pipe #<sender_id>->#<receiver_id> does not exist yet. ***
                    cerr << "*** Error: the pipe #" << senderId << "->#" << receiverId << " does not exist yet. ***\n";
                    // ret = 1;
                    // break;
                    useUserPipe0 = -1;
                    continue;
                }
                else{
                    useUserPipe0 = 1;
                    userpipe0 = userPipeRead[senderId][receiverId];
                    userPipeRead[senderId][receiverId] = -1;
                    string command = str;
                    command.erase(remove(command.begin(), command.end(), '\n'), command.end());
                    command.erase(remove(command.begin(), command.end(), '\r'), command.end());
                    // *** <receiver_name> (#<receiver_id>) just received from <sender_name> (#<sender_id>) by ’<command>’ ***
                    recv_msg = "*** " + users[userIndex].name + " (#" + to_string(users[userIndex].id) + ") just received from " + users[senderIndex].name + " (#" + to_string(senderId) + ") by '" + command + "' ***\n";
                    // broacast(msg);
                }
            }
            else
                input.push_back(cmd);
        }
        if(ret >= 0)
            break;
        if(recv_msg != "")
            broacast(recv_msg);
        if(send_msg != "")
            broacast(send_msg);
        pipePos.push_back(input.size());
        
        if(input[0].compare("setenv") == 0){
            if(input.size() < 3){
                // continue;
                break;
            }
            if(setenv(input[1].c_str(), input[2].c_str(), 1) < 0) perror("setenv");
            // continue;
            break;
        }
        else if(input[0].compare("printenv") == 0){
            if(input.size() < 2){
                break;
                // continue;
            }
                
            if(getenv(input[1].c_str()))
                cout << getenv(input[1].c_str()) << endl;
            break;
            // continue;
        }

        else if(input[0].compare("who") == 0){
            cout << "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";
            for(int i = 0; i<users.size(); i++){
                cout << users[i].id << "\t" << users[i].name << "\t" << users[i].addr << ":" << users[i].port << "\t";
                if(i == userIndex)
                    cout << "<-me";
                cout << endl;
            }
            break;
        }
        // yell <message>:
        else if(input[0].compare("yell") == 0){
            if(input.size() < 2){
                break;
                // continue;
            }
            string msg;
            msg.assign(allInput.str(), input[0].size()+1, (allInput.str()).size()-(input[0].size()+1));
            // *** <sender’s name> yelled ***: <message>
            msg = "*** " + users[userIndex].name + " yelled ***: " + msg + "\n";
            broacast(msg);
            break;
        }
        // tell <user id> <message>:
        else if(input[0].compare("tell") == 0){
            if(input.size() < 3){
                break;
                // continue;
            }
            int id = stoi(input[1]);
            int tarFd = findUserSockfdById(id);
            // cout << "tarFd =" << tarFd << endl;
            string msg;
            msg.assign(allInput.str(), input[0].size()+input[1].size()+2, (allInput.str()).size()-(input[0].size()+input[1].size()+2));
            msg = "*** " + users[userIndex].name + " told you ***: " + msg + "\n";
            if(tarFd < 0){
                // *** Error: user #<user id> does not exist yet. ***
                cerr << "*** Error: user #" << id << " does not exist yet. ***\n";
            }
            else{
                // *** <sender’s name> told you ***: <message>
                tellto(tarFd, msg);
            }
            break;
        }
        else if(input[0].compare("name") == 0){
            if(input.size() < 2){
                break;
                // continue;
            }
            int rep = 0;
            string newName = input[1];
            for(int i = 0; i<users.size(); i++){
                if((users[i].name).compare(input[1]) == 0){
                    // *** User ’<new name>’ already exists. ***
                    cout << "*** User '" << newName << "' already exists. ***\n";
                    // fprintf(stdout, "*** User '%s' already exists. ***\n", newName.c_str());
                    rep = 1;
                    break;
                }
            }
            if(rep == 0){
                users[userIndex].name = newName;
                char msg[] = "*** User from <IP>:<port> is named ’<new name>’. ***\n";
                sprintf(msg, "*** User from %s:%d is named '%s'. ***\n", (users[userIndex].addr).c_str(), users[userIndex].port, (users[userIndex].name).c_str());
                broacast(msg);
            }
            break;
            // continue;
        }

        int lastPipe[2];
        int newPipe[2];
        int numOfCmd = pipePos.size()-1;
        int numexec = 0;
        int numberedPipe0;
        int numberedPipe1;
        int isnewpipe = 0;

        for(int i = 0; i<numOfCmd; i++){
            if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                if(i != 0){
                    lastPipe[0] = newPipe[0];
                    lastPipe[1] = newPipe[1];
                }
                if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){  // not the last one or is the last one but need to pipe
                    if((i == (numOfCmd - 1)) && isNumberedPipe){
                        int j = 0;
                        for(j = 0; j < numberedPipe.size(); j++){
                            if(numberedPipe[j].line == newNumberedPipe.line){
                                numberedPipe0 = numberedPipe[j].pipe0;
                                numberedPipe1 = numberedPipe[j].pipe1;
                                break;
                            }
                        }
                        if(j == numberedPipe.size()){
                            if (pipe(newPipe) < 0){
                                cerr << "j =" << j;
                                perror(" numberedPipe1 error");
                                // cerr << "can't create pipes\n";
                            }
                            newNumberedPipe.pipe0 = newPipe[0];
                            newNumberedPipe.pipe1 = newPipe[1];
                            numberedPipe0 = newNumberedPipe.pipe0;
                            numberedPipe1 = newNumberedPipe.pipe1;
                            isnewpipe = 1;
                        }
                    }
                    else{
                        if (pipe(newPipe) < 0)
                            perror("numberedPipe2 error");
                            // cerr << "can't create pipes\n";
                    }
                }
            }

            pid_t newChild;
            if( (newChild = fork()) == -1) {
                // cerr << "cmd = " << input[0];
                // perror(" newChild fork error");
                char buff[1024];
	            int	n;
                if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                    if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){ // not the last one
                        if((i == (numOfCmd - 1)) && isNumberedPipe){ // is the last one but need to pipe
                            // dup2(numberedPipe1, STDOUT_FILENO);
                            // if(newNumberedPipe.err)
                            //     dup2(numberedPipe1, STDERR_FILENO);
                            close(numberedPipe0);
                            close(numberedPipe1);
                        }
                        else{
                            // dup2(newPipe[1], STDOUT_FILENO);
                            close(newPipe[0]);
                            close(newPipe[1]);
                        }
                    }
                    if(i != 0){ // not the first one
                        // dup2(lastPipe[0], STDIN_FILENO);
                        while ( (n = read(lastPipe[0], buff, 1024)) > 0){
                            if (write(STDOUT_FILENO, buff, n) != n)	perror("number pipe data write error");
                        }
                        if (n < 0) fprintf(stderr, "numbered pipe data read error\n");
                        close(lastPipe[0]);
                    }
                }
                // close(newPipe[0]);
                // close(newPipe[1]);
                // while ( (n = read(lastPipe[0], buff, 1024)) > 0){
                //     if (write(STDOUT_FILENO, buff, n) != n)	perror("number pipe data write error");
                // }
                // if (n < 0) fprintf(stderr, "numbered pipe data read error\n");
                // close(lastPipe[0]);

                if(useUserPipe1 == 1){
                    close(userpipe[0]);
                    close(userpipe[1]);
                }
                if(useUserPipe0 == 1){
                    while ( (n = read(useUserPipe0, buff, 1024)) > 0){
                        if (write(STDOUT_FILENO, buff, n) != n)	perror("userpipe data write error");
                    }
                    if (n < 0) fprintf(stderr, "userpipe data read error\n");
                    close(lastPipe[0]);
                }
                break;
            } 
            else if (newChild == 0) {
                /* child process */
                if(useUserPipe1 == 1){
                    if(i == (numOfCmd-1))
                        dup2(userpipe[1], STDOUT_FILENO);
                    close(userpipe[0]);
                    close(userpipe[1]);
                }
                if(useUserPipe0 == 1){
                    if(i == 0){
                        dup2(userpipe0, STDIN_FILENO);
                    }
                    close(userpipe0);
                }
                if(useUserPipe1 == -1){
                    if(i == (numOfCmd-1)){
                        int null_fd = open("/dev/null", O_WRONLY);
                        dup2(null_fd, STDOUT_FILENO);
                        close(null_fd);
                    }
                }
                if(useUserPipe0 == -1){
                    if(i ==0){
                        int null_fd = open("/dev/null", O_RDONLY);
                        dup2(null_fd, STDIN_FILENO);
                        close(null_fd);
                    }
                }

                for(int send = 0; send<=30; send++){
                    for(int rec = 0; rec<=30; rec++){
                        if(userPipeRead[send][rec] >= 0){
                            close(userPipeRead[send][rec]);
                        }
                    }
                }

                for(int j = 0; j < numberedPipe.size(); j++){
                    if(numberedPipe[j].line == line){
                        if(i == 0)
                            dup2(numberedPipe[j].pipe0, STDIN_FILENO);
                        close(numberedPipe[j].pipe0);
                        close(numberedPipe[j].pipe1);
                        break;
                    }
                }

                if(numOfCmd > 1 || isNumberedPipe){ // number of cmd more than 1(need to pipe)
                    if((i+1 < numOfCmd) || ((i == (numOfCmd - 1)) && isNumberedPipe)){ // not the last one
                        if((i == (numOfCmd - 1)) && isNumberedPipe){ // is the last one but need to pipe
                            dup2(numberedPipe1, STDOUT_FILENO);
                            if(newNumberedPipe.err)
                                dup2(numberedPipe1, STDERR_FILENO);
                            close(numberedPipe0);
                            close(numberedPipe1);
                        }
                        else{
                            dup2(newPipe[1], STDOUT_FILENO);
                            close(newPipe[0]);
                            close(newPipe[1]);
                        }
                    }
                    if(i != 0){ // not the first one
                        dup2(lastPipe[0], STDIN_FILENO);
                        close(lastPipe[0]);
                    }
                }
            
                char* arg[pipePos[i+1]-pipePos[i]+1];
                for(int j = pipePos[i], k = 0; j<pipePos[i+1]; j++, k++) {
                    char *c = new char[input[j].length() + 1];
                    strcpy(c, input[j].c_str());
                    arg[k] = c;
                }
                arg[pipePos[i+1]-pipePos[i]] = NULL;
                
                if(execvp(arg[0], arg) < 0){
                    fprintf(stderr, "Unknown command: [%s].\n", arg[0]);
                    exit(0);
                }
                    
            } else {
                /* parent process */
                
                users[userIndex].env.childs.push_back(newChild);
                if(numOfCmd > 1){ // number of cmd more than 1(need to pipe)
                    if(i+1 < numOfCmd){ // not the last one
                        close(newPipe[1]);
                    }
                    if(i != 0){ // not the first one
                        close(lastPipe[0]);
                    }
                }

                // signal(SIGCHLD, sig_chld);
                
                if(i == numOfCmd-1){
                    for(int j = 0; j < numberedPipe.size(); j++){
                        if(numberedPipe[j].line == line){
                            close(numberedPipe[j].pipe0);
                            close(numberedPipe[j].pipe1);
                            numberedPipe.erase(numberedPipe.begin()+j, numberedPipe.begin()+j+1);
                        }
                    }
                    // if(isNumberedPipe == 0 && useUserPipe1 == 0)
                    if(isNumberedPipe == 0)
                        while (users[userIndex].env.numOfWait < numOfCmd);
                    if(isNumberedPipe){
                        isNumberedPipe = 0;
                    }
                    if(isnewpipe){
                        numberedPipe.push_back(newNumberedPipe);
                        isnewpipe = 0;
                    }
                    users[userIndex].env.numOfWait = 0;
                    (users[userIndex].env.childs).clear();

                    if(useUserPipe1 == 1){
                        // close(userpipe[0]);
                        close(userpipe[1]);
                    }
                    if(useUserPipe0 == 1){
                        close(userpipe0);
                    }
                }
            }
        }
        break;
    }

    if(ret != 0){
        cerr << "% ";
        // store env
        users[userIndex].env.fileRed = fileRed;
        users[userIndex].env.savedFd = savedFd;
        users[userIndex].env.numPipe = numPipe;
        users[userIndex].env.numberedPipe = numberedPipe;
        users[userIndex].env.line = line;
        string str = getenv("PATH");
        users[userIndex].env.path = str;
    }
    
    setenv("PATH", path_backup, 1);
    dup2(stdout_backup, STDOUT_FILENO);
    dup2(stdin_backup, STDIN_FILENO);
    dup2(stderr_backup, STDERR_FILENO);
    close(stdout_backup);
    close(stdin_backup);
    close(stderr_backup);

    fflush(stdout);
    return(ret);
}

int main(int argc, char **argv){
    socklen_t clilen;
    struct sockaddr_in	cli_addr, serv_addr;
    int port = atoi(argv[1]);

    string welcome = "****************************************\n** Welcome to the information server. **\n****************************************\n";
    
    signal(SIGCHLD, sig_chld);
    signal(SIGPIPE, sig_pipe);

    for(int i = 0; i<=30; i++){
        for(int j = 0; j<=30; j++){
            userPipeRead[i][j] = -1;
        }
    }

    //  Open a TCP socket (an Internet stream socket).
    if ( (msock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        perror("server: can't open stream socket");
    // set flag SO REUSEADDR
    int opt = 1;
    if (setsockopt(msock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
    }
    // Bind our local address so that the client can send to us.
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port        = htons(port);
    if (bind(msock, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) < 0);
        perror("server: can't bind local address");
    listen(msock, 30); // maximum client

    nfds = getdtablesize();
    FD_ZERO(&afds);
    FD_SET(msock, &afds);

    while(1){
        memcpy(&rfds, &afds, sizeof(rfds));

        if (select(nfds, &rfds, (fd_set *)0, (fd_set *)0, (struct timeval *)0) < 0){
            if(errno == EINTR)
                continue;
            perror("select");
        }
            
        if (FD_ISSET(msock, &rfds)) {
            int	ssock;
            clilen = sizeof(cli_addr);
            ssock = accept(msock, (struct sockaddr *)&cli_addr, &clilen);
            if (ssock < 0)
                perror("accept");
            FD_SET(ssock, &afds);
            cout << ssock << " accept\n";
            tellto(ssock, welcome);
            
            // add new user
            char cli_addr_str[INET_ADDRSTRLEN];
            inet_ntop( AF_INET, &(cli_addr.sin_addr), cli_addr_str, INET_ADDRSTRLEN );
            userinfo newUser = createNewUser(cli_addr_str, ntohs(cli_addr.sin_port), ssock);
            users.push_back(newUser);
            sort(users.begin(), users.end(), less_than_id());

            char loginmsg[] = "*** User ’<user name>’ entered from <IP>:<port>. ***\n";
            cout << (newUser.name).c_str() << endl;
            cout << (newUser.addr).c_str() << endl;
            cout << newUser.port << endl;
            sprintf(loginmsg, "*** User '%s' entered from %s:%d. ***\n", (newUser.name).c_str(), (newUser.addr).c_str(), newUser.port);
            broacast(loginmsg);
            tellto(ssock, "% ");
        }
        for (int fd=0; fd<nfds; ++fd)
            if (fd != msock && FD_ISSET(fd, &rfds)){
                int userIndex = findUserIndexBySockfd(fd);
                if(npshell(userIndex) == 0){
                    // *** User ’<user name>’ left. ***
                    cout << users[userIndex].fd << " end\n";
                    string msg = "*** User '" + users[userIndex].name + "' left. ***\n";
                    broacast(msg);
                    int id = users[userIndex].id;
                    for(int i=1; i<=30; i++){
                        if(userPipeRead[id][i] >= 0){
                            // cout << id << " " << i << " = " << userPipeRead[id][i] << endl;
                            close(userPipeRead[id][i]);
                            userPipeRead[id][i] = -1;
                        }
                        if(userPipeRead[i][id] >= 0){
                            // cout << i << " " << id << " = " << userPipeRead[i][id] << endl;
                            close(userPipeRead[i][id]);
                            userPipeRead[i][id] = -1;
                        }
                    }
                    users.erase(users.begin() + userIndex);
                    close(fd);
                    FD_CLR(fd, &afds);
                }
                else{
                    cout << users[userIndex].fd << " comes\n";
                    FD_CLR(fd, &rfds);
                }
            }

    }
}
