/*
 * A proxy with caching
 *
 *Name: Xiaoyu Wang  Andrew ID: xiaoyuw
 *
 *This proxy listens to the request from clients and forward
 *the request to the requested server. Then it feed back the 
 *server's response to the client.
 *
 *When a connection request is received, the proxy create a 
 *unique thread for the client. In this way, the proxy serves
 *lots of requests concurrently.
 *
 *However, since every received data from server will be stored
 *in a cache, if the requested URL was already in cache, then the 
 *the proxy will not connect to the server.
 */

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*use linked list to form the cache*/
typedef struct cache_block
{
	struct cache_block *next;
	char *obj;
	char *objtag;
	int objsize;
	
}cacheblock;

/*the head of the linked list, record overall information of the cache*/
typedef struct cache_head
{
	struct cache_block *first;
	struct cache_block *last;
	int usage;
}cachehead;


/* You won't lose style points for including these long lines in your code */
static  char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static  char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static  char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static  char *connection = "Connection: close\r\n";
static  char *proxy_connection = "Proxy-Connection: close\r\n";

/*Global Variable*/
sem_t r,w;
cachehead *head;

/*function declare*/
void *thread(void *vargp);
int serve(int fd);
void clienterror(int fd, char *cause, char *errnum, 
	char *shortmsg, char *longmsg) ;
void parse_url(char *url,char *host,char *query,char *port);
void cache_ini(cachehead * head);
void cache_addLast(cachehead *head,cacheblock *newnode);
void cache_store(cachehead *head,cacheblock *newnode);
void cache_delete(cachehead *head,cacheblock *toDel);
cacheblock *cache_search(cachehead *head, char *url);


int main(int argc, char**argv)
{
	
	struct sockaddr_in clientaddr;
	socklen_t clientlen=sizeof(struct sockaddr_in);
	pthread_t tid;
	int listenfd,*connfdp;
	char* port;

	Signal(SIGPIPE,SIG_IGN);


	if(argc!=2){
		fprintf(stderr,"usage: %s <port>\n",argv[0]);
		exit(0);
	}

    /*initialization work*/    
	head=(cachehead *)Malloc(sizeof(cachehead));
	cache_ini(head);	
	sem_init(&w,0,1);
	sem_init(&r,0,1);
	
	port=argv[1];	
	listenfd=Open_listenfd(port);
	
	while(1){
		
		connfdp=(int*)Malloc(sizeof(int));
		*connfdp=Accept(listenfd,(SA*) &clientaddr,&clientlen);
		Pthread_create(&tid,NULL,thread,connfdp);

	}    

	return 0;
}

/**
 * thread - proxy serves clients concurrently in different thread
 */
 void *thread(void *vargp){

 	int connfd=*((int*) vargp);
 	Pthread_detach(Pthread_self());
 	Free(vargp); 

 	//serve the client: including forwarding to server and feedback
	serve(connfd);	

	Close(connfd);
	return NULL;
}


/**
 *serve - the process of proxy serving clients
 */
int serve(int connfd){
 	char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
 	rio_t clientrio, serverrio;
 	char port[MAXLINE];
 	char host[MAXLINE], query[MAXLINE];
	int serverfd; //file discriptor to server
	int len;

	//read the request line
	Rio_readinitb(&clientrio,connfd);
	
	if(rio_readlineb(&clientrio,buf,MAXLINE)<0){
		printf("Read get request error!\n");
		return 0;
	}
		
	sscanf(buf,"%s %s %s",method,url,version);

	if(strcasecmp(method,"GET")){  

		printf("not GET method\n" );
		return 0;
	}
	

	cacheblock *hit=cache_search(head,url);

	if(hit!=NULL){//object in cache
				
	    if(rio_writen(connfd,hit->obj,hit->objsize)<0){
		  printf("Write to client error1!\n");
		  return 0;
	    }
		

		return 1;

	}else{//object not in cache
		
		cacheblock *newnode=(cacheblock*)Malloc(sizeof(cacheblock));//create a newnode


		parse_url(url,host,query,port);		

	//forward to server
	//connect to server		
		if((serverfd=open_clientfd(host,port))<0){			
			return 0;
		}		

	//write request to server
		sprintf(buf,"GET %s HTTP/1.0\r\n",query);
		Rio_writen(serverfd,buf,strlen(buf));

		sprintf(buf,"Host: %s\r\n",host);
		Rio_writen(serverfd,buf,strlen(buf));

		Rio_writen(serverfd,user_agent_hdr,strlen(user_agent_hdr));
		Rio_writen(serverfd,accept_hdr,strlen(accept_hdr));
		Rio_writen(serverfd,accept_encoding_hdr,strlen(accept_encoding_hdr));
		Rio_writen(serverfd,connection,strlen(connection));
		Rio_writen(serverfd,proxy_connection,strlen(proxy_connection));
	Rio_writen(serverfd,"\r\n",strlen("\r\n"));//terminate line	


	//read from server, store object information in a node and send to client
	int objsize=0;
	char tmp[MAX_OBJECT_SIZE]; //temprorily store data in stack
	Rio_readinitb(&serverrio,serverfd);
	
	while((len=rio_readlineb(&serverrio,buf,MAXLINE))!=0){

		if(rio_writen(connfd,buf,len)<0){
		  printf("Write to client error2!\n");
		  return 0;
	    }		
		memcpy(tmp+objsize,buf,len);
		objsize+=len;
	}
	
  //if the object size is in range, store it in cache
	if(objsize < MAX_OBJECT_SIZE){ 
		//store url in newnode tag
		newnode->objtag=(char *)Malloc(strlen(url)+1); 
		memcpy(newnode->objtag,url,strlen(url)+1);

		newnode->objsize=objsize; //mark size

		newnode->obj=(char *)Malloc(objsize);//store object			
		memcpy(newnode->obj,tmp,objsize);
		newnode->next=NULL;	

		P(&w);
		cache_store(head,newnode);
		V(&w);

	}else{
		Free(newnode);
	}
	Close(serverfd);

  }
   return 1;
}

/**
 *parse_url - url should be parsed to aquire information about
 *host name, host query path, and port.
 */
 void parse_url(char *url,char *host,char *query,char *port){
 	char *p,*q,*r;
 	char  URL[MAXLINE];
 	URL[0]='\0';
 	strcpy(URL,url);

 	if((p=strstr(URL,"//"))!=NULL){
 		p+=2;
 	}else{
 		p=URL;
 	}
	if((q=strstr(p,"/"))!=NULL){//has suffix
		strcpy(query,q);
		*q='\0';
	 	if((r=strstr(p,":"))!=NULL){//has port
	 		strcpy(port,r+1);
	 		*r='\0';
	 		strcpy(host,p);

	 	}else{//no port
	 		strcpy(port,"80");
	 		strcpy(host,p);
	 	}

	 }else{//no suffix
	 	strcpy(query,"/");
	 	if((r=strstr(p,":"))!=NULL){//has port
	 		strcpy(port,r+1);
	 		*r='\0';
	 		strcpy(host,p);

	 	}else{//no port
	 		strcpy(port,"80");
	 		strcpy(host,p);

	 	}
	 }	


}

/**
 *cache_ini - actually, it initials the head, nothing to do 
 *with the block body
 */
 void cache_ini(cachehead *head){

 	head->first=NULL;
 	head->last=NULL;
 	head->usage=0;
 }

/**
 *cache_store - insert a cache block in the linked list
 */
 void cache_store(cachehead *head,cacheblock *newnode){
	//enough space in cache, no eviction
 	if(newnode->objsize < MAX_CACHE_SIZE-(head->usage)){
    	cache_addLast(head,newnode);//most recently used
    }else{//eviction happens
    	
    	cacheblock *tmp=head->first;
    	cacheblock *pre;
    	
    	while(MAX_CACHE_SIZE-(head->usage) < newnode->objsize){
    		pre=tmp;
    		
    		tmp=tmp->next;
    		cache_delete(head,pre);
    	}
    	cache_addLast(head,newnode);
    	
    }

}

/**
 * cache_addLast - add the cache block at the end of the linked list
 */
 void cache_addLast(cachehead *head,cacheblock *newnode){

 	if(head->last==NULL){
 		head->last=newnode;
 		head->first=newnode;
 		head->usage+=newnode->objsize;
 	}else{
 		head->last->next=newnode;
 		head->last=newnode;
 		head->usage+=newnode->objsize;
 	}


 }

/**
 * cache_delete -  delete a node in the linked list
 */
 void cache_delete(cachehead *head,cacheblock *toDel){
 	cacheblock *tmp=head->first;
 	cacheblock *pre;
 	if(tmp==NULL) return;
 	else if(toDel==tmp){
 		if(tmp->next==NULL){
 			head->last=NULL;
 		}
 		head->first=tmp->next;			
 		head->usage-=tmp->objsize;
 		Free(tmp->objtag);
		Free(tmp->obj);	
		Free(tmp);
	}
	else{
		while(tmp!=toDel){
			pre=tmp;
			tmp=tmp->next;
		}
		pre->next=tmp->next;
		head->usage-=tmp->objsize;
		Free(tmp->objtag);
		Free(tmp->obj);
		Free(tmp);
		
	}
}

/**
 *cache_search - search a node in the list according to the url.
 *If the node is found, then it should be moved to the last.
 *By this way, the least recently used cache block will always at front.
 */
cacheblock *cache_search(cachehead *head, char *url){
 	cacheblock *tmp=head->first;	
 	
 	if(tmp==NULL){
 		return NULL;		
 	}else{
 		while(tmp!=NULL){

 			if(strcasecmp(url,tmp->objtag)==0)
 				break;
 			else{ 				
 				tmp=tmp->next;
 			}

 		}
 		if(tmp==NULL) return NULL;
		else{//found in cache, move to the last
			cacheblock *clone= (cacheblock *)Malloc(sizeof(cacheblock));
			clone->next=NULL;
			clone->objsize=tmp->objsize;
			clone->objtag=(char *)Malloc(strlen(tmp->objtag)+1);
			memcpy(clone->objtag,tmp->objtag,strlen(tmp->objtag)+1);
			clone->obj=(char *) Malloc(tmp->objsize);
			memcpy(clone->obj,tmp->obj,tmp->objsize);

			
			cache_delete(head,tmp);
			cache_addLast(head,clone);
			
			return clone;
		}
	}
}