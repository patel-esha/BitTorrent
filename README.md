Guys please let me know if you need any help with the logging, i provided examples but have yet to integrate it with the peer class, idealy each class would have a memeber logger created at runtime

# Testing Setup
To set up the development environment for this project, make sure to:
* change the IP address in the connectToPeer method in Peer.java to your local IP.
* create multiple configurations/instances, which is straightforward if you're in Clion. Make sure to add a program 
argument for each one that is the port number you want that instance to run on. Each instance should have a different 
port number. For example:
  * Instance 1: Program Arguments: 5000
  * Instance 2: Program Arguments: 5001
  * Instance 3: Program Arguments: 5002
* make sure the working directory path for each configuration is in the cmake-build-debug folder.
* run these configurations simultaneously to simulate multiple peers in the network.
