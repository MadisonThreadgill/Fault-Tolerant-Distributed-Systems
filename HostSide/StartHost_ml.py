import csv
import socket
import sys
import numpy as np
from sys import exit
import ctypes
import json
import struct
import random
#HOST = "10.159.65.31"
HOST = socket.gethostname()

#HOST = "128.62.20.81"
PORT = 6000

# Open the CSV file
with open('../MNIST_Data/mnist_test.csv', newline='') as csvfile:
    reader = csv.reader(csvfile)

    # Read a single row of the CSV file
    row_num = 1
    row_data = []
    row_num_data = []
    for row in reader:
        if row_num == 2:    # row number we want to read
            row_data = row
            break
        row_num += 1

# Output the columns of the row as an array
row_data = str(row_data)

# Now we need to send this array to an available TivaC microcontroller
#row_data = ['222', '253', '253', '253', '253', '253', '253', '253', '253', '253', '253', '160', '15', '0', '0', '0', '0', '0', '0', '0', '0', '254', '253', '253', '253', '189', '99', '0', '32', '202', '253', '253', '253', '240', '122', '122', '190', '253', '253', '253', '174']
# Find a microcontroller to send it to
# Create a TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
# Bind the socket to the port
server_address = (HOST, PORT)
print(f'Starting up on {server_address[0]} port {server_address[1]}' )
sock.bind(server_address)

ipAddr = sock.getsockname()
print(f'IP Address: {ipAddr}')


# Listen for incoming connections
sock.listen(1)


while True:
    # Wait for a connection
    print('Waiting for a connection')
    connection, client_address = sock.accept()

    try:
        print(f'Connection from {client_address}')

        # Receive the data in small chunks and retransmit it
        #while True:
        data = connection.recv(16)  # data received should say if the client is available ("ready")
        data = data.decode("utf-8")
        print('received', data)
        if data == "Ready":
            print('Client is ready to receive data')
            connection.sendall(row_data.encode("utf-8"))
            # Go back to just sending 10 numbers at a time
            # try to do that for the 50 
            print("All elements sent")
                # send ML array to device
                # have device run inference
                # send results back to server
        else:
            print(f'Client {client_address} is not ready to receive data')
            break
            
    finally:
        # Clean up the connection
        connection.close()
