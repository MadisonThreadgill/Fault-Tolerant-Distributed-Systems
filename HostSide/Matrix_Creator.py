import sys
import numpy as np
import random

def string_to_matrix(s, rows, cols):
    # Split the string into a list of integers
    nums = list(map(int, s.split()))
    # Convert the list to a matrix with the given shape
    matrix = [nums[i:i+cols] for i in range(0, len(nums), cols)][:rows]
    return matrix

def generate_sizes():
    # Generate random sizes for the matrices
    rows1 = random.randint(2, 5)
    cols1 = random.randint(2, 5)
    rows2 = cols1  # The number of columns in the first matrix must match the number of rows in the second matrix
    cols2 = random.randint(2, 5)
    return rows1, cols1, rows2, cols2

def generate_matrices(rows1, cols1, rows2, cols2):
    # Create the first matrix and fill it with random numbers
    matrix1 = [[random.randint(1, 10) for j in range(cols1)] for i in range(rows1)]

    # Create the second matrix and fill it with random numbers
    matrix2 = [[random.randint(1, 10) for j in range(cols2)] for i in range(rows2)]
    return matrix1, matrix2

def print_matrices(rows1, cols1, rows2, cols2, matrix1, matrix2):
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

    print("Resulting Matrix Size:")
    print(f"{rows1} x {cols2}")

def create_data_to_send(rows1, cols1, rows2, cols2, matrix1, matrix2):
    metadata = []
    metadata.append(rows1)
    metadata.append(cols1)
    metadata.append(rows2)
    metadata.append(cols2)

    data_to_send = []
    data_to_send.append(metadata)
    data_to_send.append(matrix1)
    data_to_send.append(matrix2)
    return data_to_send

def multiply_matrices(matrix1, matrix2):
    # Multiply the matrices together
    result = [[sum(a*b for a,b in zip(row,col)) for col in zip(*matrix2)] for row in matrix1]
    return result

def print_result(result):
    print("Result matrix is:")
    print(result)

