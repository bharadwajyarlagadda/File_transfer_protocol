The project description is as follows:
- There will be one server
- There will be only one client.cpp file but there will be 10 number of clients using clientconfig file.
- All the clients get connected to the server.
- The client will already have some files with him.
- The files are represented in the form of a file vector.
- The file vector contains the information about the files that the client have.
- If the client has file 1, then the position 0 in the file vector will be '1'.
- Now, when the client requires any file, he requests the server the same.
- The server checks all the clients about the file. The server follows an algorithm in finding out the client: it first check the client whose id is greater than the requested client; if not found, it wraps around and check from the beginning.
- After the server find out the client, it sends out the same information to the requested client.
- Then both the clients establish their own connection and initiate the file transfer.
- After the client recieves the file, it closes off the connection.
