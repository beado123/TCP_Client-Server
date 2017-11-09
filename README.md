# A simple TCP client&server 
A simple TCP client and server which creates a pipe between two computers with IPv4 or IPv6 and supports operations that are client-based such as PUT(upload a file from client to server), GET(obtain a file from server to client), DELETE(delete a file in server from client remotely), LIST(list all files from server in client).       

## Getting Started
Clone the repository and
```
make
```
## Server Usage
```
./server <port number>
```
## Client Usage
### Operations
PUT: upload a file to server
```
./client <IPV4 adress>:<port number> PUT Server_side_filename local_filename
```
GET: download a file from server to client
```
./client <IPV4 adress>:<port number> GET Server_side_filename local_filename
```
DELETE: delete a specified file in server
```
./client <IPV4 adress>:<port number> DELETE Server_side_filename 
```
LIST: list all the files in server
```
./client <IPV4 adress>:<port number> LIST
```
