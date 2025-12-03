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

## Isnt the argument the peer numeber 1001-1009??? The port number is determined in the peerinfo.cfg file

# Bugs 
**Host Name**
* info.hostName = "localhost";  //  Hardcoded: Should use actual hostname 

Should be:
* info.hostName = host;   Use the hostname from file

**IP**
* inet_pton(AF_INET, "192.168.0.7", &serverAddr.sin_addr);  // ⚠️ Hardcoded IP

Should use peerInfo.hostName or resolve it properly


**Bitfield encoding in sendBitfield()** 

* Spec says: "The first byte corresponds to piece indices 0–7 from high bit to low bit"
* But we have:   

for (size_t i = 0; i < bitfield.size(); i++)

msg[5 + i] = bitfield[i] ? 1 : 0;