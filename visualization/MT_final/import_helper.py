import sys
import os

parent_directory = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

if parent_directory not in sys.path:
    sys.path.append(parent_directory)


from helper import *
from config import *
