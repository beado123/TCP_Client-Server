# TCP client&server Simulator
This is a simple TCP client&server simulator for transmitting files between computers.

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
