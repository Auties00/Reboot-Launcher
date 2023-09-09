# Port Forwarding


## What is Port Forwarding?

Port forwarding is a networking technique used to allow external devices or applications to communicate with a specific device or service within a private local network.

## Why Port Forwarding is Necessary

We need to open the port where the game server is running to make sure that other players can connect to your game server.

## Instructions

### 1. Set a static IP

Set a static IP on the PC hosting the game server and copy it for later:

- [Windows 11](https://pureinfotech.com/set-static-ip-address-windows-11/)
- [Windows 10](https://pureinfotech.com/set-static-ip-address-windows-10/)


### 2. Log into Your Router

You'll need to access your router's web interface at 192.168.1.1.
You might need a username and a password to log in: refer to your router's manual for precise instructions.

### 3. Find the Port Forwarding Section

Once logged in, navigate to the port forwarding section of your router's settings. 
This location may vary from router to router, but it's typically labelled as "Port Forwarding," "Port Mapping," or "Virtual Server."
Refer to your router's manual for precise instructions.

### 4. Add a Port Forwarding Rule

Now, you'll need to create a new port forwarding rule. Here's what you'll typically need to specify:

- **Service Name:** Choose a name for your port forwarding rule (e.g., "Fortnite Game Server").
- **Port Number:** Enter 7777 for both the external and internal ports.
- **Protocol:** Select the UDP protocol.
- **Internal IP Address:** Enter the static IP address you set earlier.
- **Enable:** Make sure the port forwarding rule is enabled.

### 5. Save and Apply the Changes

After configuring the port forwarding rule, save your changes and apply them. 
This step may involve clicking a "Save" or "Apply" button on your router's web interface.

### 6. Try hosting a game!
