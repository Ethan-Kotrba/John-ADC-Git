from read_raw_usb import Raw_Read
from New_Parse_Data import Parse_Raw_Data
import os
import argparse


def Get_Args():
    parser = argparse.ArgumentParser(description="Args")
    
    # Add positional arguments (required, no -- prefix)
    parser.add_argument("port", type=str, help="USB Port", default='/dev/ttyACM0')
    
    # Parse the arguments from the command line
    args = parser.parse_args()

    return args
    

def main():

    args = Get_Args()

    cwd = os.getcwd()
    Output_Dir = os.path.join(cwd, "PC_Side_Code", "Collected_Data")


    Reader = Raw_Read(output=Output_Dir, port=args.port)
    Reader.main()

    Filename = Reader.Output_Filename

    Parser = Parse_Raw_Data(file_dir=Output_Dir, filename=Filename)
    Parser.main()


if __name__ == "__main__":
    main()





