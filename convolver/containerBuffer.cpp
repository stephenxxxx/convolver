#include ".\ContainerBuffer.h"

template <typename T>
CChannelBuffer<T>::CChannelBuffer(const unsigned int s) : nContainers(s)
{
	containers = new T[s];
	ZeroMemory(containers, sizeof(T) * s);
};

template <typename T>
CChannelBuffer<T>::~CChannelBuffer(void)
{ 
	delete [] containers;
};

template <typename T>
CChannelBuffer<T>::CChannelBuffer(const CChannelBuffer& cb)
{
	copy(cb);
};

template <typename T>
CChannelBuffer<T>&
CChannelBuffer<T>::operator= (const CChannelBuffer &cb)
{
	if (this != &cb)
	{
		delete [] containers;
		copy(cb);
	}
	return *this;
};

template <typename T>
void
CChannelBuffer<T>::copy(const CChannelBuffer &cb)
{
	nContainers = cb.nContainers;
	containers = new T[cb.nContainers];
	for (unsigned int i = 0; i != cb.nContainers; i++)
		containers[i] = cb.containers[i];
	//memcpy(containers, cb.containers, sizeof(T) * cb.nContainers);
};


template <typename T>
CContainerBuffer<T>::CContainerBuffer(const unsigned int c, const unsigned int s ) : nChannels(c)
{
	channels = new CChannelBuffer<T>(c);
	for (unsigned int i = 0; i != c; i++)
	{
			channels[i].containers = new T[s];
			ZeroMemory(channels[i].containers, sizeof(T) * s);
	}
		//channels.push_back(CChannelBuffer<T>(s));
};

template <typename T>
CContainerBuffer<T>::~CContainerBuffer()
{

	delete [] channels;
}

//template <typename T> // T will normally be float, or perhaps double, depending on the DFT routines used
//class CContainerBuffer
//{
//public:
//
//	const unsigned int nChannels;	// number of channels
//	const unsigned int nContainers;  // length of the buffer
//
//	T** containers;		// 2d nChannels x nContainers array holding containers
//
//	CContainerBuffer(unsigned int Channels, unsigned int Containers);
//	~CContainerBuffer(void);
//
//	CContainerBuffer(const CContainerBuffer& sb);				// copy constructor
//	CContainerBuffer& operator= (const CContainerBuffer& sb);	// copy assignment
//
//};
//
//template <typename T>
//CContainerBuffer<T>::CContainerBuffer(const unsigned int Channels, const unsigned int Containers) : nChannels(Channels), nContainers(Containers)
//{
//	containers = new T*[nChannels];
//	for (unsigned int i = 0; i != nChannels; i++)
//	{
//		containers[i] = new T[nContainers];
//		ZeroMemory(containers[i], sizeof(T) * nContainers);
//	}
//};
//
//template <typename T>
//CContainerBuffer<T>::~CContainerBuffer(void)
//{
//
//	for (unsigned int i = 0; i != nChannels; i++)
//	{
//		delete [] containers[i];
//	}
//
//	delete [] containers;
//};
//
//template <typename T>
//CContainerBuffer<T>::CContainerBuffer(const CContainerBuffer& sb) // copy constructor
//{
//	nChannels = sb.nChannels;
//	nContainers = sb.nContainers;
//
//	containers = new T*[nChannels];
//	for (unsigned int i = 0; i != nChannels; i++)
//	{
//		containers[i] = new T[nContainers];
//		for (unsigned int j = 0; j != nContainers; j++)
//			containers[i][j] = sb.containers[i][j];
//	}
//};
//
//template <typename T>
//CContainerBuffer<T>& CContainerBuffer<T>::operator= (const CContainerBuffer& sb) // copy assignment
//{
//	if (this != &sb)
//	{
//		this->~CContainerBuffer();
//
//		nChannels = sb.nChannels;
//		nContainers = sb.nContainers;
//
//		containers = new T*[nChannels];
//		for (unsigned int i = 0; i != nChannels; i++)
//		{
//			containers[i] = new T[nContainers];
//			for (unsigned int j = 0; j != nContainers; j++)
//				containers[i][j] = sb.containers[i][j];
//		}
//	}
//	return *this;
//
//};
