#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <ctime>
#include <string>
#include <cstring>
using namespace std;

#define MAX_DATA_SIZE 1200
#define SYN "SYN"
#define ACK "ACK"
#define END "END\n"

int sockfd;
struct addrinfo hints, *servinfo, *p;
struct sockaddr_storage their_addr;
socklen_t addr_len;


void reliablyReceive(unsigned short int myUDPport, char* destinationFile);

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char** argv)
{
    unsigned short int udpPort;
    
    if(argc != 3)
    {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }
    
    udpPort = (unsigned short int)atoi(argv[1]);
    
    reliablyReceive(udpPort, argv[2]);
}

char * stripSeqNumber(char * buf, int &bytesRead) {
    int i=0;

    while(buf[i] != '\n') {
        i++;
    }
    bytesRead = bytesRead - (i+1);
    return &buf[i+1];
}

int setup_UDP(unsigned short int port) {
    int rv;
    //int numbytes;
    //char s[INET6_ADDRSTRLEN];
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP


    string port_str = to_string(port);
    if ((rv = getaddrinfo(NULL, port_str.c_str(), &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (::bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) { // weird Mac thing!
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    printf("listener: waiting to recvfrom...\n");

    addr_len = sizeof their_addr;

    return 0;
}

int initialize_TCP() {
    int numbytes;
    char buf[4];
    buf[3] = '\0';

    if ((numbytes = recvfrom(sockfd, buf, 3, 0,
        (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom");
        exit(1);
    }

    if (memcmp(buf,"SYN", 3) != 0) {
        printf("Received: %s\n", buf);
        cout << "Not a SYN... terminating.\n";
        return -1; // indicates termination
    }

    cout << "SYN <--\n" << "ACK -->\n";

    if ((numbytes = sendto(sockfd, ACK, 3, 0,
         (struct sockaddr *)&their_addr, addr_len)) == -1) {
        perror("sender: sendto");
        exit(1);
    }
    return 0;
}

void reliablyReceive(unsigned short int myUDPport, char* destinationFile) {
    int numbytes;
    int bytesRead;
    int DUPACKctr = 0;
    //try to open file 
    cout<< "Its reaching here 1.\n";
    ofstream myFile(destinationFile); 
    cout<< "its reaching here 2.\n";
    if (!myFile.is_open()) {
        cout << "reliablyReceive: Unable to open file.\n";
        return;
    }

    if (setup_UDP(myUDPport) != 0) {
        cout << "reliablyReceive: UDP socket setup failed... terminating.\n";
        return;// -1;
    }

    if (initialize_TCP() == -1) {
        cout << "reliablyReceive: TCP connection could not be initialized... terminating.\n";
        return;// -1;
    }

    char buf[MAX_DATA_SIZE];
    unsigned long long int last_SEQ = 0;
    bool running = true; // set to false when END packet received
    while (running) { 
	cout<< "Its reaching here 3. \n";
        if ((bytesRead = recvfrom(sockfd,buf, MAX_DATA_SIZE, 0,
            (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom"); 
	   cout<< "Somethings going wrong\n";
            exit(1);
        }
       cout << "Packet size:  \n";
        unsigned long long int SEQ_num;
        sscanf(buf, "%llu", &SEQ_num);
        //printf("SEQ_num is %llu and last_SEQ is %llu\n", SEQ_num, last_SEQ);
        char ACK_msg[MAX_DATA_SIZE];
        unsigned long long int nextSeq = last_SEQ + 1;
        char * msg = stripSeqNumber(buf, bytesRead);
            myFile.write(msg, bytesRead);
		            
            

        if (memcmp(buf,END,4) == 0) {
            cout << "Got a termination packet!\n";
            running = false;
        }



        if (SEQ_num > nextSeq) {
            // we're missing a packet! resend the last ACK.
            if (DUPACKctr < 20) {
                sprintf(ACK_msg, "%llu", last_SEQ);
                //cout << "PKT" << SEQ_num << " <--\n";
                //cout << "ACK" << ACK_msg << "-->" << endl;
                if ((numbytes = sendto(sockfd, ACK_msg, strlen(ACK_msg), 0,
                    (struct sockaddr *)&their_addr, addr_len)) == -1) {
                    perror("sender: sendto");
                    exit(1);
                }   
            }
            DUPACKctr++;
        }
        else if (SEQ_num == nextSeq) {
            // in order packet! write it to file.
            DUPACKctr = 0;
            // send an ACK!
            sprintf(ACK_msg, "%llu", nextSeq);
            if ((numbytes = sendto(sockfd, ACK_msg, strlen(ACK_msg), 0,
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
                perror("sender: sendto");
                exit(1);
            }
            last_SEQ++; // change the last_SEQ number 
	   cout << "Its reaching here 5" << "<--\n";
	   cout << "Its reaching here 4" << "<--\n";
            myFile.write(msg, bytesRead);
	   cout << "Its reaching here 4" << "<--\n";
            myFile.flush();
        }
    }

    close(sockfd);
}
