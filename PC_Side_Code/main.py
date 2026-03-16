from read_raw_usb import Raw_Read
from New_Parse_Data import Parse_Raw_Data
import os



def main():
    cwd = os.getcwd()
    Output_Dir = os.path.join(cwd, "PC_Side_Code", "Collected_Data")


    Reader = Raw_Read(output=Output_Dir)
    Reader.main()

    Filename = Reader.Output_Filename

    Parser = Parse_Raw_Data(file_dir=Output_Dir, filename=Filename)
    Parser.main()


if __name__ == "__main__":
    main()





