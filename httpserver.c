#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>


#define SIZE 2049

void code(int responseCode, int client){
	switch(responseCode){
		case 200:
			write_n_bytes(client,"HTTP/1.1 200 OK\r\nContent-Length:  3\r\n\r\nOK\n", 42);
			break;
	
		case 201:
			write_n_bytes(client, "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n", 51);
			break;
	
		case 400:
			write_n_bytes(client, "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n", 60);
			break;
			
		case 403:
			write_n_bytes(client, "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", 61);
		
		case 404:
			write_n_bytes(client, "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", 58);
			break;
		
		case 500:
			write_n_bytes(client, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal Server Error\n", 80);
			break;
		
		case 501:
			write_n_bytes(client, "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot Implemented\n", 69);
			break;
			
		case 505:
			write_n_bytes(client, "HTTP/1.1 505 Version Not Supported\r\nContent-Length: 22\r\n\r\nVersion Not Supported\n", 81);
			break;
		
		default:
			break;
	}
}


typedef struct HTTPrequest{
	char *method; //HTTP method
	char *URI; //HTTP URI (file)
	char *version; //HTTP/1.1 only
	char *header; //HTTP header-field

}HTTPrequest;

void myread(int fd, char *buffer){
	char temp;
	int bytes_read;
	int index = 0;
	
	do{
		bytes_read = read(fd, &temp, 1);
		
		if (bytes_read < 0){return;}
		
		buffer[index++] = temp;
		
	}while(strstr(buffer, "\r\n\r\n") == NULL && bytes_read >= 0);
	
	return;
}

void free_helper(HTTPrequest *parse){
	if (parse->method != NULL){
		free(parse->method);
		parse->method = NULL;
	}
	if (parse->URI != NULL){
		free(parse->URI);
		parse->URI = NULL;
	}
	if (parse->version != NULL){
		free(parse->version);
		parse->version = NULL;
	}
	if (parse->header != NULL){
		free(parse->header);
		parse->header = NULL;
	}
	return;
}

int isFile(char *filename, int client_socket){
	struct stat fileInfo;
	int check = stat(filename, &fileInfo);
	if (S_ISDIR(fileInfo.st_mode) > 0){
		code(403, client_socket);
		return 1;
	}else if (check == -1){return check;}
	return check;
}

int getLength(char *header, int client_socket){
	char Pattern[] = ".*?Content-Length: ([0-9]*)";
	char *size = NULL;
	regex_t regex;
	regmatch_t matches[2];

	
	if (regcomp(&regex, Pattern, REG_EXTENDED) != 0){
		code(400, client_socket); 
		return -1;
	}
	
	if (regexec(&regex, header, 2, matches, 0) == 0){
		int length = matches[1].rm_eo - matches[1].rm_so + 1;
		size = (char *)malloc(length * sizeof(char));
		strncpy(size, header + matches[1].rm_so, length);
		size[matches[1].rm_eo - matches[1].rm_so] = '\0';
	}else{
		code(400, client_socket);
		regfree(&regex);
		free(size);
		return -1;
	}
	
	regfree(&regex);
	int put_length = atoi(size);
	free(size);
	return put_length;
	
}

// Reads from client desired file and buffers contents back. Using snprintf
// @param file parsed URI. We make basic checks then begin to read from its file descriptor
// @param client_socket used for error calls
int get(char *file, int client_socket){

	int bytes_read = 0;
	char buffer[SIZE + 1] = {'\0'};
	
	if (isFile(file, client_socket) != 0){
		return 1;
	}
	
	int fd = open(file, O_RDONLY);
	
	if (fd == -1){
		code(404, client_socket);
		return 1;
	}
	
	bytes_read = read_n_bytes(fd, buffer, SIZE);
	
	close(fd);
	
	char serv_response[SIZE + 1] = {'\0'};
	int return_size = snprintf(serv_response, SIZE, "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n%s\n",bytes_read, buffer);
	
	if (return_size == -1){
		code(500, client_socket);
		return 1;
	}

	int written = write_n_bytes(client_socket, serv_response, strlen(serv_response));
	
	if (written == -1){
		code(500, client_socket);
		return 1;
	}
	
	return 0;
}

// Either creating file or truncating bytes to file depending on its existence within directory.
// Dynamic allocation of memory to buffer to avoid overflow of stdin bytes. This is done in a double do/while 
// loop where number of bytes are read then written and so on.
// @param client_socket used for error calls 
// @param file reading from our parsed URI, check if its directory or if it cannot be opened 
// @param header reading from our parsed header-field, obtaining desired amount of bytes to write into file
int put(int client_socket, char *file, char *header){

	char Message[SIZE + 1] = {'\0'};
	int size = getLength(header, client_socket);
	int read_bytes, written_bytes  = 0;
	int totalBytes = 0;
	int flag = isFile(file,client_socket);
	
	if (flag > 0){ return 1;}
	
	if (size < 0){ return 1;}
	
	int fd = open(file, O_CREAT | O_WRONLY | O_TRUNC);
	
	if (fd < 0){
		code(400, client_socket);
		return 1;
	}
	
	do{
		read_bytes = read(client_socket, Message, 1);
		
		if (read_bytes < 0){
			code(500, client_socket);
			close(fd);
			return 1;
		}else if (read_bytes == 0){break;}
		
		totalBytes += read_bytes;
		written_bytes = 0;
		
		do{
			int bytes_to_write = write(fd, Message, read_bytes - written_bytes);
						
			if (bytes_to_write <= 0){
				code(500, client_socket);
				close(fd);
				return 1;
			}
			
			written_bytes += bytes_to_write;
			
		}while(written_bytes < read_bytes);

	}while(read_bytes > 0 && totalBytes < size);
	
	if (flag < 0){code(201, client_socket);}
	else if(flag == 0){code(200, client_socket);}
	
	close(fd);
	
	return 0;
}

// Parsing the buffer containing the HTTP request using regex functions. We create substrings
// of the method, file, and header-field (if some or any exist) and call appropriate later functions for 
// GET or PUT. 
// @param buf contains our HTTP request to be parsed using regex
// @param *parse pointer to typedef struct HTTPrequest containing char* of method, URI, and header
int HTTPsubstring(char buf[], HTTPrequest *parse, int client_socket){
	int response = 0;
	
	char Pattern[] = "^(GET|PUT) /([0-9A-Za-z.-/]{2,64}) (HTTP/[0-9].[0-9]*)(\r\n|)(.*[0-9.A-Za-z.-]{1,128}: .*[0-9.A-Za-z.-]{1,128}|)";
	
	regex_t regex;
	regmatch_t matches[6];

	if (regcomp(&regex, Pattern, REG_EXTENDED) != 0){ //checking for compilation
		code(400, client_socket); //EDIT AND MAKE SURE ITS CORRECT ERROR CODE
		return 1;
	}
	

	if(regexec(&regex, buf, 6, matches, 0) == 0){ //regexec here for portions of HTTPrequest
	
	//METHOD
		int method_length = matches[1].rm_eo - matches[1].rm_so + 1; 
		parse->method = (char *)malloc(method_length * sizeof(char)); //malloc here
		strncpy(parse->method, buf + matches[1].rm_so, method_length); //HTTP method here
		parse->method[matches[1].rm_eo - matches[1].rm_so] = '\0';
	
	//URI
		int URI_length = matches[2].rm_eo - matches[2].rm_so + 1; 
		parse->URI = (char *)malloc(URI_length * sizeof(char)); //malloc here
		strncpy(parse->URI, buf + matches[2].rm_so, URI_length); //HTTP URI (file.txt) here
		parse->URI[matches[2].rm_eo - matches[2].rm_so] = '\0';
		
	//VERSION
		int version_length = matches[3].rm_eo - matches[3].rm_so + 1; 
		parse->version = (char *)malloc(version_length * sizeof(char)); //malloc here
		strncpy(parse->version, buf + matches[3].rm_so, version_length);
		
		parse->version[matches[3].rm_eo - matches[3].rm_so] = '\0';
		
		
		
	}else{ //if not correct HTTP command switch case 400
		regfree(&regex);
		code(400, client_socket);
		return 1;
	}
	
	if(strcmp(parse->version,"HTTP/1.1") != 0){ //Not HTTP/1.1
		//free everthing malloced
		code(505, client_socket);
		regfree(&regex); //free regex here
		free_helper(parse);
		return 1;	
	}
	
	if(strcmp(parse->method, "GET") == 0){ //if method GET send file to get function
		response = get(parse->URI, client_socket);
		regfree(&regex); //free regex here
		free_helper(parse);
		return response;
	}
	
	else if(strcmp(parse->method, "PUT") == 0){ //else if method PUT grab headerfield 
	//HEADER
		int header_length = matches[5].rm_eo - matches[5].rm_so + 1;
		parse->header = malloc(header_length * sizeof(char)); //malloc here
		strncpy(parse->header, buf + matches[5].rm_so, header_length); //HTTP header field here
		parse->header[matches[5].rm_eo - matches[5].rm_so] = '\0';
		
		response = put(client_socket, parse->URI, parse->header);
		regfree(&regex);
		free_helper(parse);
		return response;
	}
	
	return response;
}


int main(int argc, char *argv[]) {

	char buf[SIZE + 1] = {'\0'};
	
	HTTPrequest req = {
		.method = NULL,
		.URI = NULL,
		.version = NULL,
		.header = NULL,
	};
	HTTPrequest *ptr = &req;
	int response = 0;

	Listener_Socket socket; // initializing socket to listen to port

	if (argc < 2) { // no port given or more than allowed
		fprintf(stderr, "Invalid Command");
		return 1;
	}

	int port = atoi(argv[1]); // taking port number from command line args

	if (port > 65535 || port < 1024){
		fprintf(stderr, "Invalid Port Number\n");
		return 1;
	}

	int check = listener_init(&socket, port);


	if (check < 0) {
		fprintf(stderr, "Invalid Port\n");
		return 1;
	}
	
	while (true){

		int client_verify = listener_accept(&socket); // accept connections

		if (client_verify < 0) { // error check
			fprintf(stderr, "Invalid Verification\n");
			return 1;
		}
		
		myread(client_verify, buf);
		
		response = HTTPsubstring(buf, ptr, client_verify);
		close(client_verify);
		
	}
	
	return response;
}
