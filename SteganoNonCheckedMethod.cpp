#include "SteganoNonCheckedMethod.h"
#include "BitmapHeader.h"

#include <QBitArray>
#include <QRgb>
#include <QDebug>
#include <iostream>
#include <fstream>
#include <algorithm>

const char* CSteganoNonCheckedMethod::footer = "\x0F\x1F\xAE\xD3\xE9\xA4\xCD\x01";

CSteganoNonCheckedMethod::CSteganoNonCheckedMethod(void)
{

}

CSteganoNonCheckedMethod::CSteganoNonCheckedMethod(const CSteganoNonCheckedMethod&)
{

}

CSteganoNonCheckedMethod::~CSteganoNonCheckedMethod(void)
{

}

QSharedPointer<CSteganoMethod> CSteganoNonCheckedMethod::createSteganoNonCheckedMethod()
{
    return QSharedPointer<CSteganoMethod>(new CSteganoNonCheckedMethod());
}

//returns offset from the beginning of file to the end of the image
void CSteganoNonCheckedMethod::findEndOfImage(std::fstream& imageFile)
{
    //seek to a place where the footer should be
    imageFile.seekg (0, std::ios::end);
    imageFile.seekg (-c_FooterLength, std::ios::cur);
    if(imageFile)
    {
        //read footer
        char realFooter[c_FooterLength+1];
        imageFile.read(realFooter, c_FooterLength);
        //end with 0 for strcmp can be used
        realFooter[c_FooterLength] = 0;

        if(strcmp(footer,realFooter) == 0)
        {
            //footer is present
            //read size of hidden file
            imageFile.seekg (-c_FooterLength-static_cast<int>(sizeof(int)), std::ios::cur);
            unsigned int filesize = 0;
            imageFile.read((char*)&filesize, sizeof(int));
            if(!imageFile)
            {
                //nothing hidden
                imageFile.clear();
                imageFile.seekg (0, std::ios::end);
            }
            else
            {
                //seek to a place where base image ends
                imageFile.seekg (-filesize-static_cast<int>(sizeof(int)), std::ios::cur);
            }
        }
        else
        {
            //nothing is hidden, seek to a place where base image ends - to the end of file
            imageFile.seekg (0, std::ios::end);
        }
    }
    else
    {
        //imageFile is shorter than c_FooterLength bits, no data can be hidden there
        imageFile.clear();
        imageFile.seekg (0, std::ios::end);
    }
}

int CSteganoNonCheckedMethod::findHiddenData(std::fstream& imageFile)
{
    //check the file contains a bitmap
    //read the BMP fileheader
    SBitmapFileHeader h;
    imageFile.read((char*)&h, static_cast<std::streamsize>(sizeof(SBitmapFileHeader)));
    if(imageFile && h.bfType == BF_TYPE)
    {
        imageFile.seekg (0, std::ios::end);
        int filesize = imageFile.tellg();
        imageFile.seekg (h.bfSize, std::ios::beg);
        //no need to read size of hidden file, it can be computed.
        return (filesize-h.bfSize-c_FooterLength-static_cast<int>(sizeof(int)));
    }
    else
    {
        //not a BMP file, try to read footer
        imageFile.clear();
        imageFile.seekg (0, std::ios::end);
        int filesize = imageFile.tellg();

        findEndOfImage(imageFile);
        int endOffset = imageFile.tellg();
        int dataLength = filesize - endOffset - static_cast<int>(sizeof(int)) - c_FooterLength;
        return dataLength;
    }
}

void CSteganoNonCheckedMethod::encrypt(QString ImageFilePath, QString ImageSaveFilePath, QString DataFilepath, PArgsList SteganoParameters)
{
    Q_UNUSED(SteganoParameters);
    std::fstream sourceImageFile(ImageFilePath.toStdString().c_str(), std::fstream::in | std::fstream::binary);
    std::fstream dataFile(DataFilepath.toStdString().c_str(), std::fstream::in | std::fstream::binary);
    std::fstream outputImageFile(ImageSaveFilePath.toStdString().c_str(), std::fstream::out | std::fstream::binary);

    if(!sourceImageFile || !dataFile || !outputImageFile)
    {
        //cannot open one of the files
        emit encryptFinished(false);
        emit errorOccurred("Cannot access one of the following: source file, data, output file!");
        return;
    }
    std::streamsize count = 0;
    int bytesOfSourceImageToReadLeft = 0;
    findEndOfImage(sourceImageFile);
    bytesOfSourceImageToReadLeft = sourceImageFile.tellg();
    if(bytesOfSourceImageToReadLeft>0)
    {
        //rewrite source image (without hidden data if there is any)
        sourceImageFile.clear();
        sourceImageFile.seekg (0, std::ios::beg);
        int dataToReadLength = bytesOfSourceImageToReadLeft;
        do
        {
            sourceImageFile.read(buffer, std::min(c_BufferSize,bytesOfSourceImageToReadLeft));
            count = sourceImageFile.gcount();
            outputImageFile.write(buffer,count);
            bytesOfSourceImageToReadLeft -= count;
            int percent = static_cast<double>((dataToReadLength-bytesOfSourceImageToReadLeft))/dataToReadLength*50;
            emit progressChanged(percent);
            //usleep(100000);
        }
        while(count != 0 && bytesOfSourceImageToReadLeft>0);
    }
    else
    {
        emit encryptFinished(false);
        emit errorOccurred("Invalid base image!");
        return;
    }

    dataFile.seekg(0,std::ios::end);
    int infoFileSize = dataFile.tellg();
    dataFile.seekg(0,std::ios::beg);
    int bytesRead = 0;

    //write whole info file
    do
    {
        dataFile.read(buffer, c_BufferSize);
        count = dataFile.gcount();
        outputImageFile.write(buffer,count);
        bytesRead+= count;
        int percent = static_cast<double>(bytesRead)/infoFileSize*50+50;
        emit progressChanged(percent);
        //usleep(100000);
    }
    while(count != 0);

    //clear flags preventing from calling tellg
    dataFile.clear();

    //write size to file
    outputImageFile.write((char*)&infoFileSize,sizeof(int));

    //write footer to indicate that something is hidden
    outputImageFile.write(footer,c_FooterLength);

    emit encryptFinished(true);
    return;
}

void CSteganoNonCheckedMethod::decrypt(QString ImageFilePath, QString SaveFilepath, PArgsList SteganoParameters)
{
    Q_UNUSED(SteganoParameters);
    std::fstream file(ImageFilePath.toStdString().c_str(), std::fstream::in | std::fstream::binary);

    if(file.is_open()) { // file opened
        int dataToReadLength = findHiddenData(file);

        //restart
        int endOffset = file.tellg();
        file.seekg(endOffset,std::ios::beg);

        if(dataToReadLength <= 0)
        {
            emit decryptFinished(false);
            emit errorOccurred("Nothing is hidden!");
            return;
        }

        //open output file
        std::fstream filewrite(SaveFilepath.toStdString().c_str(), std::fstream::out | std::fstream::binary);
        if(!filewrite)
        {
            emit decryptFinished(false);
            emit errorOccurred("Unable to open file to write!");
            return;
        }

        //write the data
        int DataToReadLeft = dataToReadLength;
        unsigned int count = 0;
        do
        {
            file.read(buffer, std::min(c_BufferSize,DataToReadLeft));
            count = file.gcount();
            filewrite.write(buffer,count);
            DataToReadLeft -= count;
            int percent = static_cast<double>((dataToReadLength-DataToReadLeft))/dataToReadLength*100;
            emit progressChanged(percent);
            //usleep(100000);
        }
        while(count != 0 && DataToReadLeft>0);

        emit decryptFinished(true);
        return;
    }
    else
    {
        emit decryptFinished(false);
        emit errorOccurred("Unable to open image file!");
        return;
    }
}

void CSteganoNonCheckedMethod::makePreview(QString imageFilePath, QString data, PArgsList steganoParameters)
{
    Q_UNUSED(data);
    Q_UNUSED(steganoParameters);

    PImage pImage(new QImage(imageFilePath));
    emit previewFinished(pImage);
}

QSharedPointer<CSteganoMethod> CSteganoNonCheckedMethod::clone()
{
    return QSharedPointer<CSteganoMethod>(new CSteganoNonCheckedMethod(*this));
}

QString CSteganoNonCheckedMethod::getName()
{
    return QString("NonCheckedAreaOfFiles");
}

QString CSteganoNonCheckedMethod::getSupportedTypesToEncrypt()
{
    return QString("Image Files (*.png *.jpg *.bmp)");
}

QString CSteganoNonCheckedMethod::getSupportedTypesToDecrypt()
{
    return QString("");
}
