import csv
import socket
import sys
import numpy as np
from sys import exit
import ctypes
import json
import struct
import random

def string_to_matrix(s, rows, cols):
    # Split the string into a list of integers
    nums = list(map(int, s.split()))
    # Convert the list to a matrix with the given shape
    matrix = [nums[i:i+cols] for i in range(0, len(nums), cols)][:rows]
    return matrix

def compare_matrices(matrix1, matrix2):
    return np.array_equal(matrix1, matrix2)


HOST = socket.gethostname()

PORT = 6000

# Generate random sizes for the matrices
rows1 = random.randint(2, 10)
cols1 = random.randint(2, 10)
rows2 = cols1  # The number of columns in the first matrix must match the number of rows in the second matrix
cols2 = random.randint(2, 10)

# Create the first matrix and fill it with random numbers
matrix1 = [[random.randint(1, 10) for j in range(cols1)] for i in range(rows1)]

# Create the second matrix and fill it with random numbers
matrix2 = [[random.randint(1, 10) for j in range(cols2)] for i in range(rows2)]

# Print the matrices
print("Matrix 1 Size:")
print(f"{rows1} x {cols1}")
for row in matrix1:
    print(row)
    
print("\nMatrix 2 Size:")
print(f"{rows2} x {cols2}")
for row in matrix2:
    print(row)

print("Resulting Matrix Size:")
print(f"{rows1} x {cols2}")

metadata = []
metadata.append(rows1)
metadata.append(cols1)
metadata.append(rows2)
metadata.append(cols2)

data_to_send = []
data_to_send.append(metadata)
data_to_send.append(matrix1)
data_to_send.append(matrix2)

# Multiply the matrices together
result = [[sum(a*b for a,b in zip(row,col)) for col in zip(*matrix2)] for row in matrix1]
print("Result matrix is:")
print(result)

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

            # send size of the matrices to the client
            data_to_send = str(data_to_send) + "\n"
            connection.sendall(data_to_send.encode("utf-8"))
            
            print("Matrices sent to client")

            # receive the result matrix from the client
            data = connection.recv(1600)  # data received should say if the client is available ("ready")
            data = data.decode("utf-8")
            data = string_to_matrix(data, rows1, cols2)
            print('Received Matrix')
            print(data)

            if(compare_matrices(result, data)):
                print("Matrices match")
            else:
                print("Matrices do not match")

                # send ML array to device
                # send results back to server
                # match with result matrix on server
        else:
            print(f'Client {client_address} is not ready to receive data')
            break
            
    finally:
        # Clean up the connection
        connection.close()
