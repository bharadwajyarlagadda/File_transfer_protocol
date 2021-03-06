#include "common.h"
#include "client.h"
#include <pthread.h>

#define EXP_CMD_TYPE_FIRST_LEVEL 0
#define EXP_CMD_TYPE_FILE_NUMBER 1
#define EXP_CMD_TYPE_SEND_FILE_YN 2
#define EXP_CMD_TYPE_SEND_KEY     3


unsigned char exp_cmd_type = 0;

int current_asking_file_index = -1;
unsigned char current_asking_file_hash[20];
char current_asking_file[FILE_LENGTH];
int current_asking_file_next_location = 0;
int client_who_has_file = -1;

void add_one_file_block(char* buf)
{
	memcpy((current_asking_file+current_asking_file_next_location),buf,DATA_BLOCK_SIZE);
	current_asking_file_next_location += DATA_BLOCK_SIZE;
}

char FILE_VECTOR[FILE_NUMBER] = "000000000000000000000000000000000000000000000000000000000000000";

char server_sock_buf[MY_SOCK_BUFFER_LEN];
int server_sock_buf_byte_counter;


class Connection
{
public:
    int socket;
    char peerip[MAXIPLEN]; 
    int peerport;
    int peerid;	
int peer_listen_port;
	char sock_buf[MY_SOCK_BUFFER_LEN];
	int sock_buf_byte_counter;

    Connection()
    {
	socket = -1;
	peerport  = -1;
	sprintf(peerip,"%s","0.0.0.0");	
	peerid = -1;
	peer_listen_port = -1;
	sock_buf_byte_counter = 0;
    }

    Connection(int sock, const char* ip, int port)
    {
	socket =sock;
	peerport  = port;
	sprintf(peerip,"%s",ip);	
	peerid = -1;
	peer_listen_port = -1;
	sock_buf_byte_counter = 0;
    }

	Connection(int sock, const char* ip, int port, int id)
    {
	socket =sock;
	peerport  = port;
	sprintf(peerip,"%s",ip);	
	peerid = id;
	peer_listen_port = -1;
	sock_buf_byte_counter = 0;
    }
};

int init_quit = 0;
// vector containing all the active connections
vector<Connection> activeconnections;
set<int> peerstodelete;

struct option long_options[] =
{
  {"serverip",   required_argument, NULL, 's'},
  {"serverport",   required_argument, NULL, 'h'},
  {"port",   required_argument, NULL, 'p'},
  {"config",   required_argument, NULL, 'c'},
  {"id",   required_argument, NULL, 'i'},
  {"debug",   required_argument, NULL, 'd'},
  {"servername",   required_argument, NULL, 'm'},
  { 0, 0, 0, 0 }
};

fd_set master;   
fd_set read_fds; 
	
int serversockfd = -1;
int listener = -1 ;  
		
int highestsocket;
void read_config(const char*);
void read_from_activesockets();
void process_peer_message(Packet*);
void send_file_to_client(int clientid,int file_index,bool decision);
int connect_to_server(const char* IP, int port );
struct sockaddr_in myaddr;     // my address

int MYPEERID = 99;
int SERVERPORT = 51000;
int MY_LISTEN_PORT = 52000;

char SERVERNODE[100];
char SERVERIP[40];// = "127.0.0.1";

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER; 

struct filetosend{
	int peerid;
	char thisfile[FILE_LENGTH];
    int blocksent;
	int file_index;
	bool done;
	int client_socket;
};

vector<filetosend> outstandingfiles; 
struct filetosend tosend;
int key;

// search for sockets
int find_socket(int clientid)
{
	for(int i = 0; i < activeconnections.size(); i++) 
	{
		if (activeconnections[i].peerid == clientid )
		{	
			return activeconnections[i].socket;
		}
	}
	return -1;
}

// read the config file and know who you are
void read_config(const char* configfile)
{
    FILE* f = fopen(configfile,"r");

    if (f)
    {
		fscanf(f,"CLIENTID %d\n",&MYPEERID);
	    fscanf(f,"SERVERPORT %d\n",&SERVERPORT);
	    fscanf(f,"MYPORT %d\n",&MY_LISTEN_PORT);
	    fscanf(f,"FILE_VECTOR %s\n",FILE_VECTOR);

		fclose(f);

        printf("My ID is %d\n", MYPEERID);
        printf("Sever port is %d\n", SERVERPORT);
        printf("My port is %d\n", MY_LISTEN_PORT);
        printf("File vector is %s\n", FILE_VECTOR);
    }
    else
    {
        cout << "Cannot read the configfile!" << configfile << endl;;
     	fflush(stdout);
        exit(1); 
    }
}


int connect_to_server(const char* IP, int PORT)
{
    struct sockaddr_in server_addr; // peer address\n";
    int sockfd;
    
    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) 
    {
        printf("Cannot create a socket");
        return -1;
    }
    
    server_addr.sin_family = AF_INET;    // host byte order 
    server_addr.sin_port = htons(PORT);  // short, network byte order 
    inet_aton(IP, (struct in_addr *)&server_addr.sin_addr.s_addr);
    memset(&(server_addr.sin_zero), '\0', 8);  // zero the rest of the struct 

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) 
    {
         printf("Error connecting to the server %s on port %d\n",IP,PORT);
     	 sockfd = -1;
    }

    if (sockfd != -1)
    {
 	    FD_SET(sockfd, &master);
 	    if (highestsocket <= sockfd)
	    {
            highestsocket = sockfd;
	    } 
    }

	return sockfd; 
}



void send_packet_to_server(Packet *packet)
{
	int send_result = send_a_control_packet_to_socket(serversockfd, packet);
    // will help in debugging in case of trouble 
    if (send_result == -1)
    {
        printf("Oh! Cannot send packet to server!\n");
	    if (errno == EPIPE)
	    {
	        //printf("Trouble sending data on socket %d to client %d with EPIPE .. CLOSING HIS CONNECTION\n", socket2send, event->recipient);
	        //connection_close(event->recipient);
	    }
    }
    else
    {
        //printf("Ha! Sent packet to server!\n");
    }	
}

void send_packet_to_peer(Packet *packet, int clientid)
{
    int peertosend = find_socket(clientid);
	int send_result = send_a_control_packet_to_socket(peertosend, packet);
    // will help in debugging in case of trouble
    if (send_result == -1)
    {
        printf("Oh! Cannot send packet to client %d!\n", clientid);
    	if (errno == EPIPE)
	    {
	        //printf("Trouble sending data on socket %d to client %d with EPIPE .. CLOSING HIS CONNECTION\n", sockettosend, clientid);
	    }
    }
    else
    {
        //printf("Ha! Sent packet to client %d!\n", clientid);
        //printpacket((char*) packet, PACKET_SIZE);            
    }	
}

void send_a_file_block_to_peer(char *block, int clientid)
{
    int sockettosend = find_socket(clientid);
	int send_result = send_a_file_block_to_socket(sockettosend,block);
    
    // will help in debugging in case of trouble
	if (send_result == -1)
    {
        printf("Oh! Cannot send packet to client %d!\n", clientid);
    	if (errno == EPIPE)
	    {
	        //printf("Trouble sending data on socket %d to client %d with EPIPE .. CLOSING HIS CONNECTION\n", sockettosend, clientid);
	    }
    }
    else
    {
        //printf("Ha! Sent packet to client %d!\n", clientid);
        //printpacket((char*) packet, PACKET_SIZE);            
    }	
}



void send_register_to_server(int clientid)
{
    Packet packet;
    
    packet.sender = clientid;
    packet.recipient = SERVER_NODE_ID;
    packet.event_type = EVENT_TYPE_CLIENT_REGISTER;  
    packet.port_number = MY_LISTEN_PORT;
	memcpy(packet.FILE_VECTOR, FILE_VECTOR, FILE_NUMBER);

    send_packet_to_server(&packet);
}

void send_req_file_to_server(int clientid, int file_index)
{
    Packet packet;
    
    packet.sender = clientid;
    packet.recipient = SERVER_NODE_ID;
    packet.event_type = EVENT_TYPE_CLIENT_REQ_FILE;  
    packet.req_file_index = file_index;

    send_packet_to_server(&packet);
}

void send_quit_to_server(int clientid)
{
    Packet packet;
    
    packet.sender = clientid;
    packet.recipient = SERVER_NODE_ID;
    packet.event_type = EVENT_TYPE_CLIENT_QUIT;  

    send_packet_to_server(&packet);
}

void send_got_file_to_server(int clientid, int peerid, int file_index)
{
	Packet packet;
	packet.sender = clientid;
	packet.req_file_index = file_index;
	packet.peerid = peerid;	
	memcpy(packet.FILE_VECTOR, FILE_VECTOR, FILE_NUMBER);
        packet.event_type = EVENT_TYPE_CLIENT_GOT_FILE;  
        send_packet_to_server(&packet);
}


void send_req_file_to_peer(int clientid, int peerid, int file_index)
{
	Packet packet;
	packet.sender = clientid;
   	packet.recipient = peerid;
	packet.req_file_index = file_index;
	current_asking_file_next_location = 0;
	packet.event_type = EVENT_TYPE_CLIENT_REQ_FILE_FROM_PEER;  
	send_packet_to_peer(&packet,peerid);
}


void client_init(void)
{
    tosend.peerid = -1;
}

void client_got_server_name(void)
{
    struct hostent *he_server;
    if ((he_server = gethostbyname(SERVERNODE)) == NULL) 
    {
     	printf("error resolving hostnam for server %s\n",SERVERNODE);
     	fflush(stdout);
        exit(1);
    }
   
    struct sockaddr_in  server;
    memcpy(&server.sin_addr, he_server->h_addr_list[0], he_server->h_length);
    printf("SERVER IP is %s\n",inet_ntoa(server.sin_addr));
    strcpy(SERVERIP,inet_ntoa(server.sin_addr));
}


void client_got_server_IP(void)
{
	struct in_addr ipv4addr;
	inet_pton(AF_INET, SERVERIP, &ipv4addr);

    struct hostent *he_server;
	if ((he_server = gethostbyaddr(&ipv4addr, sizeof ipv4addr, AF_INET)) == NULL) 
    {
     	printf("error resolving hostnam for server %s\n",SERVERIP);
     	fflush(stdout);
        exit(1);
    }
   
    printf("SERVER name is %s\n",he_server->h_name);
}



void deleteconnection(int clientid)
{
	bool flag = true;
	vector<Connection>::iterator iter ;
	for(iter= activeconnections.begin(); iter!= activeconnections.end() && flag ; iter++)
	{
		Connection currentconn = *iter;
		if (currentconn.peerid == clientid )
		{	
			flag = false;
			break;
		}
	}
	if (iter != activeconnections.end()) 
	{
		close((*iter).socket);
		activeconnections.erase(iter);
	}
}


void process_server_message(Packet *packet)
{
    if (packet->event_type == EVENT_TYPE_SERVER_REPLY_REQ_FILE)
    {
	int client_to_give_file = packet->peerid;
	int client_asking_file = MYPEERID;
	printf("[Message from Server]: server tells client %d to get file %d from client %d on %s with listen port_number %d\n", MYPEERID, current_asking_file_index, packet->peerid, packet->peerip, packet->peer_listen_port);
	client_who_has_file = packet->peerid;
	memcpy(current_asking_file_hash,packet->hash,20);
	printf("[Message from Server]: The file's hash value is:\n");
	print_hash(current_asking_file_hash);	
	int peersd = connect_to_server(packet->peerip,packet->peer_listen_port);	
	if (peersd == -1)
	{
		printf ("\nCould not connect with the client !!!!\n");
		return;
	}		
	Connection newconn = Connection(peersd,packet->peerip,packet->peer_listen_port,packet->peerid);
	activeconnections.push_back(newconn);
	send_req_file_to_peer(MYPEERID,packet->peerid,current_asking_file_index);
	}
	else if (packet->event_type == EVENT_TYPE_SERVER_QUIT)
	{
		printf("\nserver telling me to quit!\n");
		exit(0);
	}
}

void process_peer_message(Packet *packet)
{
	if(packet->event_type == EVENT_TYPE_CLIENT_REQ_FILE_FROM_PEER)
	{
	printf("\nClient %d needs file %d from you. Should I send him? [Y/N]\n",packet->sender,packet->req_file_index);	
	fflush(stdout);	
	fflush(stdin);
	char response;
	scanf("\n%c",&response);
	fflush(stdout);	
	fflush(stdin);
	if(response == 'y' || response == 'Y')
		{
			send_file_to_client(packet->sender,packet->req_file_index,true);
		}
	else if(response == 'q' || response == 'Q')
	{
			send_quit_to_server(MYPEERID);
	}	
	else
	{
	send_file_to_client(packet->sender,packet->req_file_index,false);
	}
	}
	else
	{
		printf("\nClient#%d wants to quit...I am closing\n",packet->sender);
		int sockfd = find_socket(packet->sender);
            	close(sockfd); // bye!
            	FD_CLR(sockfd, &master); // remove from master set
		deleteconnection(packet->sender);
	}
}

void send_file_to_client(int clientid,int file_index,bool decision)
{
char file[FILE_LENGTH];
generate_file(file_index,FILE_LENGTH,file);
if(!decision){
printf("Enter any single numeric key:\n");
	scanf("%d",&key);
	for(int i =0; i<FILE_LENGTH;i++){
		file[i] = file[i]^key;
}
}
filetosend sending_file;
sending_file.peerid = clientid;
memcpy(sending_file.thisfile,file,FILE_LENGTH); 
sending_file.blocksent = 0;
sending_file.file_index = file_index;
sending_file.done = false;
sending_file.client_socket = find_socket(clientid);
if(sending_file.client_socket == -1)
	return;
outstandingfiles.push_back(sending_file);
}


void process_stdin_message(void)
{
	 if (exp_cmd_type == EXP_CMD_TYPE_FIRST_LEVEL)
	 {
         char c; 
         scanf("\n%c", &c);

		 if (c == 'f')
		 {
			 printf("please tell me what file you need, thanks.\n");
			 exp_cmd_type = EXP_CMD_TYPE_FILE_NUMBER;
		 }         
		 else if (c == 'q')
		 {
			send_quit_to_server(MYPEERID);
		 }
		 else
		 {
			 printf("wrong command. q or f. %c\n", c);
		 }
	 }
	 else if (exp_cmd_type == EXP_CMD_TYPE_FILE_NUMBER)
	 {
		 int file_index = 0;
         scanf("%d", &file_index);

		 printf("client %d need file %d\n", MYPEERID, file_index);

		 if (FILE_VECTOR[file_index] == '1')
		 {
			 printf("I have this file. \n");
		 }
		 else
		 {
		     current_asking_file_index = file_index;
		     send_req_file_to_server(MYPEERID, file_index);
		 }
		 	 
		 exp_cmd_type = EXP_CMD_TYPE_FIRST_LEVEL;
	 }
	 else if (exp_cmd_type == EXP_CMD_TYPE_SEND_FILE_YN)
	 {
		// todo: add your code here.
        // whether the user wants to send the correct file
        // the project description has more on this
	 }
	 else if (exp_cmd_type == EXP_CMD_TYPE_SEND_KEY)
	 {
		// todo: add your code here.
        // the code here will implement the xor logic
        // that simulates a data block corruption
	 }
	 else
	 {
		 printf("oops, bad read\n");
	 }

}

bool got_entire_file(void)
{
	bool flag = false;
	unsigned char hash_value[20];
	printf("The original hash value for the file is:\n");
	print_hash(current_asking_file_hash);
	find_file_hash(FILE_LENGTH,current_asking_file,hash_value);
	printf("The hash value of the file which I was sent is:\n");
	print_hash(hash_value);
	for(int i = 0;i<20;i++){
	if(hash_value[i] == current_asking_file_hash[i])
		flag = true;
	}
	return flag;
}


void read_from_activesockets(void)
{
    struct sockaddr_in remoteaddr; // peer address
    struct sockaddr_in server_addr; // peer address
    int newfd;        // newly accept()ed socket descriptor
    char buf[MAXBUFLEN];    // buffer for client data
    int nbytes;
    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    socklen_t addrlen;
    int i, j;

    if ((listener != -1) && FD_ISSET(listener,&read_fds))
    {
		// todo: add your code here.
        // listening here, what does the client do here.
	addrlen = sizeof(remoteaddr);
		if ((newfd = accept(listener, (struct sockaddr *)&remoteaddr,&addrlen)) == -1)
		{
			printf("Error accepting a new connection");
		}else{
			FD_SET(newfd, &master); 
			if (newfd > highestsocket)
				highestsocket = newfd;
			
	printf("Connection to %s port number %d on socket %d successful\n", inet_ntoa(remoteaddr.sin_addr), remoteaddr.sin_port, newfd);
		Connection newconn(newfd,  inet_ntoa(remoteaddr.sin_addr), remoteaddr.sin_port);
			activeconnections.push_back(newconn);
		}
    }

    if (FD_ISSET(fileno(stdin),&read_fds))
    {
		process_stdin_message();
    }


    if ((serversockfd != -1) && FD_ISSET(serversockfd,&read_fds) )
    {
	    nbytes = recv(serversockfd, buf, MAXBUFLEN, 0);
        // handle server response or data 		
    	if ( nbytes <= 0) 
  	    {
            // got error or connection closed by client
            if (nbytes == 0) 
       	    {
                printf("Server hung up with me. I will also quit.\n");
				exit(0);
            } 
	        else 
	        {
	            printf("client %d recv error from server \n", MYPEERID);
            }
            close(serversockfd); // bye!
            FD_CLR(serversockfd, &master); // remove from master set
            serversockfd = -1;
        }
        else
        {
        	memcpy(server_sock_buf + server_sock_buf_byte_counter, buf, nbytes);
	        server_sock_buf_byte_counter += nbytes;

			int type = server_sock_buf[0];
			int num_to_read = get_num_to_read(type);
			
			while (num_to_read <= server_sock_buf_byte_counter)
			{
			    if (type == PACKET_TYPE_CONTROL)
			    {
			        Packet* packet = (Packet*) (server_sock_buf+1); 	
			        process_server_message(packet);
				}
				else
				{
					// todo: add your code here.
				}

				remove_read_from_buf(server_sock_buf, num_to_read);
				server_sock_buf_byte_counter -= num_to_read;

				if (server_sock_buf_byte_counter == 0)
					break;

				type = server_sock_buf[0];
				num_to_read = get_num_to_read(type);
		    }
        }
    }
else
    {		
         // run through the existing connections looking for data to read
         peerstodelete.clear();
         for(int i = 0; i < activeconnections.size(); i++) 
         {
	         if (FD_ISSET(activeconnections[i].socket, &read_fds)) 
	         {
   		         nbytes = recv(activeconnections[i].socket, buf, MAXBUFLEN, 0);
                 if ( nbytes <= 0) 
	             {
                      // got error or connection closed by client
                     if (nbytes == 0) 
     		         {
                          // connection closed
                          printf("Socket %d client %d hung up\n", activeconnections[i].socket, activeconnections[i].peerid);
                     } 
	     	         else 
		             {
                           printf("Client recv error\n");
                     }
       
                     close(activeconnections[i].socket); // bye!
                     FD_CLR(activeconnections[i].socket, &master); // remove from master set
		             peerstodelete.insert(activeconnections[i].peerid);
       	         }
	             else
	             {
			memcpy(activeconnections[i].sock_buf + activeconnections[i].sock_buf_byte_counter, buf, nbytes);
			activeconnections[i].sock_buf_byte_counter += nbytes;

			int type = activeconnections[i].sock_buf[0];
			int num_to_read = get_num_to_read(type);
						
				while (num_to_read <= activeconnections[i].sock_buf_byte_counter)
				{
						if (type == PACKET_TYPE_CONTROL)
						{
					        Packet* packet = (Packet*) (activeconnections[i].sock_buf+1); 	
							if (activeconnections[i].peerid == -1)
								{
								activeconnections[i].peerid = packet->sender;
								}	
					                 process_peer_message(packet);	
							}
							else
							{
							char temp_buff[DATA_BLOCK_SIZE];				
							memcpy(temp_buff,activeconnections[i].sock_buf+1,DATA_BLOCK_SIZE);
							printf("Receiving data from Client #%d\n",activeconnections[i].peerid);
							add_one_file_block(temp_buff);
							if(current_asking_file_next_location>=FILE_LENGTH){
							printf("File transfer complete\n");
								bool file_get_value = got_entire_file();
								if(file_get_value == true){
									printf("I got the correct file from client %d\n", client_who_has_file);
									FILE_VECTOR[current_asking_file_index] = '1';
						send_got_file_to_server(MYPEERID,client_who_has_file,current_asking_file_index);
									}
								else{
									printf("I got a corrupted file from client %d\n", client_who_has_file);
									fflush(stdout);
									fflush(stdin);
						send_req_file_to_peer(MYPEERID, client_who_has_file, current_asking_file_index);
									}
							}
							}
							remove_read_from_buf(activeconnections[i].sock_buf, num_to_read);
							activeconnections[i].sock_buf_byte_counter -= num_to_read;

							if (activeconnections[i].sock_buf_byte_counter == 0)
								break;

    						type = activeconnections[i].sock_buf[0];
	    					num_to_read = get_num_to_read(type);
						}
				  }	
    	      }
	     }
		   
         for (set<int>::iterator iter = peerstodelete.begin(); iter != peerstodelete.end(); iter++)
	     {
	         deleteconnection(*iter);
	     }	 

		 if ((init_quit) && (activeconnections.size() == 0))
		 {
			 printf("All clients quit. Server quitting.\n");
			 exit(0);
		 }		
     }

}


void *thread_sending_file(void *arg)
{
while (1)
{   
sleep(2);
for(int i=0;i<outstandingfiles.size();i++)
{
char buff[DATA_BLOCK_SIZE];
if(!outstandingfiles[i].done)		
{
memcpy(buff,(outstandingfiles[i].thisfile+(outstandingfiles[i].blocksent*DATA_BLOCK_SIZE)),DATA_BLOCK_SIZE);  
pthread_mutex_lock(&mtx);
outstandingfiles[i].blocksent++;
pthread_mutex_unlock(&mtx);
int file_send = send_a_file_block_to_socket(outstandingfiles[i].client_socket,buff);
    printf("Sending block#%d of file#%d to client#%d\n",(outstandingfiles[i].blocksent),outstandingfiles[i].file_index,outstandingfiles[i].peerid);		
  	if(file_send == -1)
       	{
         printf("\nSending failed");
	}			
	if(outstandingfiles[i].blocksent == FILE_LENGTH/DATA_BLOCK_SIZE)
	{
	pthread_mutex_lock(&mtx);
	printf("File transfer complete\n");
	outstandingfiles[i].done = true;
	pthread_mutex_unlock(&mtx);
	}
	else{
	pthread_mutex_lock(&mtx);
	outstandingfiles[i].done = false;
	pthread_mutex_unlock(&mtx);
	}
	}		
}		
}
}


void client_run(void)
{
    // listen on peer connections on some port
    struct sockaddr_in remoteaddr; // peer address
    struct sockaddr_in server_addr; // peer address
    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    socklen_t addrlen;
    int i, j;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);
	
    highestsocket =0;
	
    struct timeval timeout;

	serversockfd =  connect_to_server(SERVERIP,SERVERPORT);
   
    send_register_to_server(MYPEERID);

     FD_SET(fileno(stdin), &master);

     if (fileno(stdin) > highestsocket)
     {
      	highestsocket = fileno(stdin);
     }
     // todo: add your code here for the listening socket
     // This code will double up the client as a server
     // Need to look at the server code and reuse that code here maybe
	if ((listener = socket(PF_INET, SOCK_STREAM, 0)) == -1)
	{
		printf("cannot create a socket");
		fflush(stdout);
		exit(1);
	}
	
	myaddr.sin_family = AF_INET;
	myaddr.sin_addr.s_addr = INADDR_ANY;
	myaddr.sin_port = htons(MY_LISTEN_PORT);
	memset(&(myaddr.sin_zero), '\0', 8);
	if (bind(listener, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1)
	{
		printf("could not bind to MYPORT");
		fflush(stdout);
		exit(1);
	}

	//listen
	if (listen(listener, 40) == -1)
	{
		printf("too many backlogged connections on listen");
		fflush(stdout);
		exit(1);
	}

	FD_SET(listener, &master);
	//FD_SET(listener, &read_fds);

	if (listener > highestsocket)
		highestsocket = listener;

	if (fileno(stdin) > highestsocket)
		highestsocket = fileno(stdin);
	
    // main loop
    while (1)
    {
        read_fds = master; 
	    timeout.tv_sec = 2;
		timeout.tv_usec = 0;

        if (select(highestsocket+1, &read_fds, NULL, NULL, &timeout) == -1) 
        {
            if (errno == EINTR)
    	    {
                printf("Select for client %d interrupted by interrupt...\n", MYPEERID);
    	    }
    	    else
    	    {
                printf("Select problem .. client %d exiting iteration\n", MYPEERID);
		        fflush(stdout);
                exit(1);
            }
        }
      
        read_from_activesockets();
    }		   
}


int main(int argc, char** argv)
{
    int c, option_index=0;
    char* configfile;	

	while ((c = getopt_long (argc, argv, "c:s:h:p:d:i:m:", long_options, &option_index)) != EOF)
    {
 	    switch (c)
	    {
	
			case 'c': 
					configfile = optarg;
					read_config(configfile);
					break;
			case 's': 
					memcpy(SERVERIP, optarg, strlen(optarg)); 
					client_got_server_IP();
					break;
			case 'h':
				    SERVERPORT = atoi(optarg);
					break;
			case 'p':
				    MY_LISTEN_PORT = atoi(optarg);
		    		break;
			case 'i':
				    MYPEERID = atoi(optarg);
		    		break;

			case 'm':
					memcpy(SERVERNODE, optarg, strlen(optarg)); 
					client_got_server_name();
					break;

			case 'd':
                    // what is the debug value
                    int debug = atoi(optarg);
                    printf("DEBUG LEVEL IS %d\n", debug );
                    break;
 	    }
    }  
    client_init();

    pthread_t tid;
    int res = pthread_create(&tid, NULL, thread_sending_file, NULL);
         
    if (res == 0)
  	    cout << "successfully created thread!" <<endl;	
    else
       	cout << "unsuccessful in creating thread!" <<endl;	

    client_run();

    return 0;
}

