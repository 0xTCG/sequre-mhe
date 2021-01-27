import random

import numpy as np

import utils.param as param

from utils.custom_types import zeros


def get_address(port: int) -> str:
    return f"{param.AF_PREFIX}.{port}"


def get_cache_path(pid: int, name: str) -> str:
    return f'{param.CACHE_FILE_PREFIX}_P{pid}_{name}.bin'


def get_output_path(pid: int, name: str) -> str:
    return f'{param.OUTPUT_FILE_PREFIX}_P{pid}_{name}.txt'


def get_temp_path(pid: int, name: str) -> str:
    return f'temp/temp_P{pid}_{name}.txt'


def bytes_to_arr(bytes_str: str) -> np.ndarray:
    for elem in bytes_str.split(b'.'):
        yield int(elem)


def read_vector(f, n: int, primes_bytes: int) -> np.ndarray:
    a = zeros(n)
    
    for i in range(n):
        a[i] = int(f.read(primes_bytes))
    
    return a


def read_matrix(f, nrows: int, ncols: int, primes_bytes: int) -> np.ndarray:
        a = zeros((nrows, ncols))
        
        for i in range(nrows):
            a[i][:] = read_vector(f, ncols, primes_bytes)
        
        return a    

def filter_rows(mat: np.ndarray, mask: np.ndarray) -> np.ndarray:
    return filter(mat, mask)


def rand_int(lower_limit: int, upper_limit: int) -> int:
    return np.random.randint(lower_limit, upper_limit)


def random_ndarray(base: int, shape: tuple) -> np.ndarray:
    return np.random.randint(base, size=shape)