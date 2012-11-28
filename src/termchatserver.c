/* this project is created as a school assignment by Endre Palinkas */
/* the server uses network code from the class literature "Linux Programozas" */
/* written by lecturer Gabor Banyasz & Tihamer Levendovszky, 2003  */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#define PORT "2233"
#define MAX_CHAT_CLIENTS 15

#define MAX_SOCKET_BUF 1024
#define MAX_MSG_LENGTH 80
#define MAX_NICK_LENGTH 12
#define MAX_CHANNEL_LENGTH 12

#define DISCONNECTED 0
#define WAITING_FOR_NICK 1
#define HAS_NICK_WAITING_FOR_CHANNEL 2
#define CHATTING 3

// for printing client messages to stdout
#define DEBUG

int server_socket, csock;
struct addrinfo hints;
struct addrinfo* res;
int err;
struct sockaddr_in6 addr;
socklen_t addrlen;
char ips[NI_MAXHOST];
char servs[NI_MAXSERV];

// TODO different MAX_SOCKET_BUF & MAXMSGLENGTH
char buffer[MAX_SOCKET_BUF];
char msg_to_send[MAX_SOCKET_BUF];
char reply[MAX_SOCKET_BUF];
int len;
int reuse;
int i;
const char msg_server_full[]="Sorry, the chat server is currently full. Try again later.\n";

// sockets to give to select
fd_set socks_to_process;

// chat client data type
typedef struct {
	int socket;
	char nickname[MAX_NICK_LENGTH];
	// status: DISCONNECTED, WAITING_FOR_NICK, HAS_NICK_WAITING_FOR_CHANNEL, CHATTING
	int status;
	char channel[MAX_CHANNEL_LENGTH];
} chat_client_t;

// we will hold MAX_CHAT_CLIENTS
chat_client_t chat_clients[MAX_CHAT_CLIENTS];

int StrBegins(const char *haystack, const char *beginning);
//char* ProcessClientCmd(int clientindex, const char *cmd_msg);
void ProcessClientCmd(int clientindex, const char *cmd_msg, char *reply);
void ProcessClientChanMsg(int clientindex, const char *chan_msg);
void ProcessClientPrivMsg(int clientindex, const char *priv_msg);

// this array will hold the connected client sockets
//int connected_client_socks[MAX_CHAT_CLIENTS];

// sets a socket to non-blocking
void SetNonblocking(int sock) {
	int opts = fcntl(sock, F_GETFL);
	opts = (opts | O_NONBLOCK);
	fcntl(sock, F_SETFL, opts);
}

// creates the set of sockets that select needs to iterate through
// to be called from the main loop
void BuildSelectList() {
	int i;
	// empty the set
	FD_ZERO(&socks_to_process);
	
	// add the server socket
	FD_SET(server_socket, &socks_to_process);
	
	// add the client sockets which are connected
	for(i=0; i<MAX_CHAT_CLIENTS; i++) 
		if (0 != chat_clients[i].socket) 
			FD_SET(chat_clients[i].socket,&socks_to_process);
}

// if we detect a new incoming connection, let's accept it if we have an empty slot
void HandleNewConnection(void) {
	int i;
	int client_socket;
	int getnameinfo_error;
	short connection_accepted = 0;
	
	client_socket = accept(server_socket, (struct sockaddr*)&addr, &addrlen);
	
	if (client_socket == -1)
		printf("Error occured while trying to accept connection\n");
	
	else {
		SetNonblocking(client_socket);
		// try to get client's ip:port string in a protocol-independent way, using getnameinfo()
		// we need the size of addr
		addrlen = sizeof(addr);
		// TODO: for some reason first lookup fails
		getnameinfo_error = getnameinfo((struct sockaddr*)&addr, addrlen, ips, sizeof(ips), servs, sizeof(servs), 0);
		// check if there's room for our socket
		for (i=0; (i < MAX_CHAT_CLIENTS) && (0 == connection_accepted); i++)
			if (0 == chat_clients[i].socket) {
				// we found a free slot, let's accept the client connection
				chat_clients[i].socket=client_socket;
				chat_clients[i].status=WAITING_FOR_NICK;
				if (0 == getnameinfo_error) 
					printf("Chat client connection accepted from: %s:%s. Socket descriptor: %d, Socket index: %d\n", ips, servs, client_socket, i);
				else 
					printf("Chat client connection accepted. Cannot display client address, an error occured while looking it up.\n");
				connection_accepted = 1;
			}
		}
		
		if (0 == connection_accepted) {
			// went through all slots, and we couldn't put the new connection in any
			// we need to reject this client then
			if (0 == getnameinfo_error) 
				printf("Rejecting client connection from %s:%s, no free slots.\n", ips, servs);
			else 
				printf("Rejecting client connection. Cannot display client address, an error occured while looking it up.\n");
			send(client_socket, msg_server_full, strlen(msg_server_full), 0);
			close(client_socket);
		}
}

// ProcessPendingRead() to be called when we already know that one client has data to transfer
// Data is read & sent to the other chat clients
void ProcessPendingRead(int clientindex)
{
	int bytes_read;
	//int i;
	
	do {
		// fill buffer with zeros
		bzero(buffer, MAX_SOCKET_BUF);
		// receive the data
		bytes_read = recv(chat_clients[clientindex].socket, buffer, MAX_SOCKET_BUF, 0);
		
		if (0 == bytes_read) {
			// got disconnected from this client
			// there was an EOF, and this is read as 0 byte by recv()
			printf("Disconnected from a client. Socket descriptor: %d, Socket index: %d\n", chat_clients[clientindex].socket, clientindex);
			close(chat_clients[clientindex].socket);
			chat_clients[clientindex].socket = 0;
			chat_clients[clientindex].status = DISCONNECTED;
			// start with no channel, make sure it has no garbage
			bzero(chat_clients[clientindex].channel, MAX_CHANNEL_LENGTH);
			//TODO
			//free(chat_clients[clientindex].nickname);
			break;
		}
		
		if (bytes_read > 0) {
			// the connection is healthy
			// and we read data from the client in "buffer"
			
			#ifdef DEBUG
			// add to stdout in debug mode
			printf("A client has sent: %s\n", buffer);
			#endif
			
			
			// let's see if we got a command from the client
			if ( !(StrBegins(buffer, "CMD")) ) {
				// process the client command, and prepare the reply
				//if (NULL != reply) free(reply);
				// todo mem leak?
				ProcessClientCmd(clientindex, buffer, reply);
				// send back reply to the client
				//send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
				// CHANGED, THE SUBROUTINE WILL DO THIS
				continue;
			}
			
			if ( !(StrBegins(buffer, "CHANMSG ")) ) {
				ProcessClientChanMsg(clientindex, buffer);
				continue;
			}
			
			if ( !(StrBegins(buffer, "PRIVMSG ")) ) {
				ProcessClientPrivMsg(clientindex, buffer);
				continue;
			}				
					
		}
	} while (bytes_read > 0);
}

// to be called from the main loop
// in the case select reports that there is at least 1 socket that needs to be read
void ProcessSocketsToRead() {
	int i;
	// if there's a new connection request, select() will mark the socket as readable
	if (FD_ISSET(server_socket, &socks_to_process)) 
		HandleNewConnection();
	
	// let's iterate through the sockets
	// if a socket's file descriptor is in the socks_to_process set, then it needs to be read
	for(i=0; i<MAX_CHAT_CLIENTS; i++)
		if (FD_ISSET(chat_clients[i].socket, &socks_to_process))
			ProcessPendingRead(i);
}

int main() {

	// timeout for select
	struct timeval select_timeout;
	int num_of_sockets_to_read;
	
	// let's assemble the local address, which is needed for the binding. we will use getaddrinfo() for this
	// AI_PASSIVE, so the addresses will be INADDR_ANY or IN6ADDR_ANY
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;

	
	// int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
	err = getaddrinfo(NULL, PORT, &hints, &res);
	if(err != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
		return -1;
	}

	// TODO error msg
	if(res == NULL) return -1;
	
	// creating the server socket now
	// int socket(int domain, int type, int protocol);
	server_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (server_socket < 0) {
	  perror("socket");
	  // TODO error msg
	  return -1;
	}

	// we allow reusing of sockets (SO_REUSEADDR). Socket level (SOL_SOCKET)
	// int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
	reuse = 1;
	setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	// set the socket to non-blocking
	SetNonblocking(server_socket);
	
	// bind the server socket to the address, based on the reply of getaddrinfo()
	// int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	if (bind(server_socket, res->ai_addr, res->ai_addrlen) < 0) {
	  perror("bind");
	  return -1;
	}

	// let's listen for a connection
	// int listen(int sockfd, int backlog);
	if(listen(server_socket, 5) < 0) {
		perror("listen");
		return 1;
	}
	
	// we don't need the address linked list generated by getadrrinfo() anymore
	freeaddrinfo(res);
	
	// initialize the clients array
	for (i=0; i<MAX_CHAT_CLIENTS; i++) {
		chat_clients[i].socket=0;
		chat_clients[i].status=DISCONNECTED;
	}
	
	
	// allocate memory for the client socket list
	/*
	memset((char *) &connected_client_socks, 0, sizeof(connected_client_socks));
*/

	// main loop, we iterate through the sockets
	// accept connections if needed, read them if needed, giving them a small timeout	
	while (1)
	{
		BuildSelectList();
		select_timeout.tv_sec = 1;
		select_timeout.tv_usec = 0;
		// run the select. it will return if 
		// a) it can read from the set of sockets in socks_to_process (or EOF if disconnected)
		// b) after timeout
		num_of_sockets_to_read = select(FD_SETSIZE, &socks_to_process, (fd_set *) 0, (fd_set *) 0, &select_timeout);
		
		// select has modified socks_to_process, only those remain which can be read without blocking
		if (0 == num_of_sockets_to_read) {
			printf("No sockets to read.\n");
			fflush(stdout);
		}
		else {
			ProcessSocketsToRead();
		}
	}
	
	// maybe some graceful quit when keypressed Q
	// close(server_socket);
	return 0;
}

// returns 0 if haystack begins with beginning (case sensitive), -1 if not
int StrBegins(const char *haystack, const char *beginning) {
	int i;
	if (NULL == haystack || NULL == beginning) 
		return -1;
	if (sizeof(beginning) > sizeof(haystack))
		return -1;
	
	// let's compare until the end of beginning
	for (i=0; beginning[i]!='\0'; i++) {
		if (haystack[i]!=beginning[i]) return -1;
	}
	
	// we got this for, so they match
	return 0;
}

// process a command that we got from a chat client
// cmd_msg example: CMDNICK Johnny\0
// reply will be updated to the server reply, this should be sent to the client by the caller
// some reply examples:
//  CMDERROR Nick already taken.\0
//  CMDNICKOK newnick
//  CMDCHANNELOK newchannel
//  CMDERROR Unknown command.\0
void ProcessClientCmd(int clientindex, const char *cmd_msg, char *reply) {
		// reset reply string
		bzero(reply, MAX_SOCKET_BUF);
				
		if ( !(StrBegins(buffer, "CMDNICK ")) ) {
			char newnick[MAX_NICK_LENGTH];
			sscanf(buffer, "CMDNICK %s", newnick);
			
			// is this nick really new?
			if (!strcmp(newnick, chat_clients[clientindex].nickname)) {
				sprintf(reply, "CMDERROR Your nick is already %s", newnick);
				send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
				return;
			}			
			
			// check if nick is taken
			for (i=0; i<MAX_CHAT_CLIENTS; i++) {
				if (!strcmp(newnick, chat_clients[i].nickname)) {
					sprintf(reply, "CMDERROR The %s nickname is already taken.", newnick);
					send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
					return;
				}
			}
			
			// ok, it wasn't taken
			// send the current nick updates to the other people in the channel
			// CHANUPDATECHNICK oldnick newnick
			for (i=0; i<MAX_CHAT_CLIENTS; i++) {
				if ( i!=clientindex && !strcmp(chat_clients[clientindex].channel, chat_clients[i].channel) ) {
					sprintf(reply, "CHANUPDATECHANGENICK %s %s", chat_clients[clientindex].nickname, newnick);
					send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
					return;
				}
			}
			
			// now we can change the nick of the person
			strcpy(chat_clients[clientindex].nickname, newnick);
			if ( WAITING_FOR_NICK == chat_clients[clientindex].status )
				chat_clients[clientindex].status = HAS_NICK_WAITING_FOR_CHANNEL;
			sprintf(reply, "CMDNICKOK %s", newnick);
			send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
	
			return;
		}
		
		if ( !(StrBegins(buffer, "CMDCHANNEL ")) ) {
			char new_channel[MAX_CHANNEL_LENGTH];
			sscanf(buffer, "CMDCHANNEL %s", new_channel);
			
			// send CHANUPDATELEAVE leavernick to other people in the old channel
			for (i=0; i<MAX_CHAT_CLIENTS; i++) {
				if ( i!=clientindex && !strcmp(chat_clients[i].channel, new_channel) ) {
					sprintf(reply, "CHANUPDATELEAVE %s", chat_clients[clientindex].nickname);
					send(chat_clients[i].socket, reply, strlen(reply), 0);
					return;
				}
			}
			
			// send CHANUPDATEJOIN joinernick to other people in the new channel
			for (i=0; i<MAX_CHAT_CLIENTS; i++) {
				if ( i!=clientindex && !strcmp(chat_clients[i].channel, new_channel) ) {
					sprintf(reply, "CHANUPDATEJOIN %s", chat_clients[clientindex].nickname);
					send(chat_clients[i].socket, reply, strlen(reply), 0);
					return;
				}
			}
			
			// now we can change the channel of the person
			strcpy(chat_clients[clientindex].channel, new_channel);
			chat_clients[clientindex].status = CHATTING;
			sprintf(reply, "CMDCHANNELOK %s", new_channel);
			send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
			
			// send all nicks to the new joiner
			// format: CHANUPDATEALLNICKS nick1 nick2 etc
			sprintf(reply, "CHANUPDATEALLNICKS", new_channel);			
			for (i=0; i<MAX_CHAT_CLIENTS; i++) {
				if ( !strcmp(chat_clients[i].channel, new_channel) ) {
					// we found someone in the channel
					// if reply is not too long yet, add it
					if ((sizeof(reply) + 1 + sizeof(chat_clients[i].nickname) < MAX_SOCKET_BUF ) {
						strcat(reply, " ");
						strcat(reply, chat_clients[i].nickname);
					}
					// reply too long, so let's send the last reply, and start building a new one
					else {
						send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
						// reset reply string
						bzero(reply, MAX_SOCKET_BUF);
						sprintf(reply, "CHANUPDATEALLNICKS %s", chat_clients[i].nickname);
					}
					// we went through all the users, the reply is ready to be sent
					// for small channels, this is the only CHANUPDATEALLNICKS reply
					// for big channels, this is the last one
					send(chat_clients[clientindex].socket, reply, strlen(reply), 0);
					return;
				}
			}		
			return;
			
		}
		
		// if the CMD line didn't fit any of the commands, it has wrong syntax, reply this.
		sprintf(reply, "CMDERROR Unknown command.");
}

// process a channel message that we got from a chat client
// if we can accept the channel message, we relay it to others in the channel
// and also to the sender - this serves as a acknowledgement of delivery
// if we cannot accept the channel message, then we send a CHANMSGERROR
void ProcessClientChanMsg(int clientindex, const char *chan_msg) {
		// reset msg_to_send string
		bzero(msg_to_send, MAX_SOCKET_BUF);	
		
		// we don't accept channel messages, if the client hasn't set a nickname yet
		if (chat_clients[clientindex].status == WAITING_FOR_NICK) {
			sprintf(msg_to_send, "CHANMSGERROR Please set a nickname, and set the channel first.");
			send(chat_clients[clientindex].socket, msg_to_send, strlen(msg_to_send), 0);
			return;
			}
		
		// client has set a nickname, but they also have to set the channel before sending channel msgs
		if (chat_clients[clientindex].status == HAS_NICK_WAITING_FOR_CHANNEL) {
			sprintf(msg_to_send, "CHANMSGERROR Please set the channel first.");
			send(chat_clients[clientindex].socket, msg_to_send, strlen(msg_to_send), 0);
			return;
		}
		
		// ok, so the client has a nick and is in a channel, we accept the channel message
		char *channel = chat_clients[clientindex].channel;
		
		// we got the message in format: CHANMSG hi there
		// we send it back to the people in the channel in format: CHANMSGFROM sendernick hi there
		// let's build the new message to send based on the original
		sprintf(msg_to_send, "CHANMSGFROM %s %s", chat_clients[clientindex].nickname, chan_msg+8);		
		
		// go through every client, and send them the message if they're in the particular channel
		// even the source, so he knows that their message has been delivered
		for (i=0; i < MAX_CHAT_CLIENTS; i++) {
			 if (!strcmp(channel, chat_clients[i].channel))
				send(chat_clients[i].socket, msg_to_send, strlen(msg_to_send), 0);
		}	
}

// process a private message that we got from a chat client
// format: PRIVMSG targetnick message
// if we can accept the channel message, we relay it to the recepient
// in format: PRIVMSGFROM sourcenick message
// sender will get back a PRIVMSGOK targetnick message as an acknowledgement
// or a PRIVMSGERROR error message
void ProcessClientPrivMsg(int clientindex, const char *priv_msg) {	
		char target_nick[MAX_NICK_LENGTH];
		char message[MAX_MSG_LENGTH];
		
		// reset msg_to_send string
		bzero(msg_to_send, MAX_SOCKET_BUF);	
		
		// we don't accept private messages, if the client hasn't set a nickname yet
		if (chat_clients[clientindex].status == WAITING_FOR_NICK) {
			sprintf(msg_to_send, "PRIVMSGERROR Please set a nickname before sending a private message.");
			send(chat_clients[clientindex].socket, msg_to_send, strlen(msg_to_send), 0);
			return;
		}
		
		// get the target nick & the actual message
		sscanf(priv_msg, "PRIVMSG %s %[^\n]", target_nick, message);
		// go through every client
		for (i=0; i < MAX_CHAT_CLIENTS; i++) {			
			if (!strcmp(target_nick, chat_clients[i].nickname)) {
				// we found the target nick, send him the PRIVMSGFROM sourcenick message
				sprintf(msg_to_send, "PRIVMSGFROM %s %s", chat_clients[clientindex].nickname, message);
				send(chat_clients[i].socket, msg_to_send, strlen(msg_to_send), 0);
				// now send the ack to the sender, PRIVMSGOK targetnick message
				bzero(msg_to_send, MAX_SOCKET_BUF);
				sprintf(msg_to_send, "PRIVMSGOK %s %s", target_nick, message);
				send(chat_clients[clientindex].socket, msg_to_send, strlen(msg_to_send), 0);
				return;
			}
		}

		// if we get this far, that means we couldn't find the target nick
		sprintf(msg_to_send, "PRIVMSGERROR Can't deliver message, no user named '%s' is online.", target_nick);
		send(chat_clients[clientindex].socket, msg_to_send, strlen(msg_to_send), 0);		
}
