/*=auto=========================================================================

Portions (c) Copyright 2005 Brigham and Women's Hospital (BWH) All Rights Reserved.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Program:   3D Slicer
Module:    $RCSfile: itkMRMLIDImageIO.cxx,v $
Date:      $Date: 2006/03/17 15:10:09 $
Version:   $Revision: 1.18 $

=========================================================================auto=*/


#include "itkMRMLIDImageIO.h"
#include "itkMetaDataObject.h"

#include "vtkMRMLDiffusionTensorVolumeNode.h"
#include "vtkMRMLDiffusionWeightedVolumeNode.h"
#include "vtkMRMLScene.h"

#include <vtkDataArray.h>
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkPointData.h>


namespace itk {

MRMLIDImageIO
::MRMLIDImageIO()
{
  this->Scheme = "";
  this->Authority = "";
  this->SceneID = "";
  this->NodeID = "";
}

MRMLIDImageIO
::~MRMLIDImageIO()
{
}

bool
MRMLIDImageIO
::IsAVolumeNode(const char* filename)
{
  vtkMRMLVolumeNode *node;

  node = this->FileNameToVolumeNodePtr(filename);

  if (node)
    {
    return true;
    }

  return false;
}

vtkMRMLVolumeNode *
MRMLIDImageIO
::FileNameToVolumeNodePtr(const char* filename)
{
  // if this is a MRML node, then filename will be encoded 
  // with a "slicer" scheme.  There are two possibilities:
  //
  // slicer:<scene id>/<node id>                  - local slicer
  // slicer://<hostname>/<scene id>/<node id>     - remote slicer
  //
  // What about multiple instances of slicer on a machine? (ports?)
  //
  std::string fname = filename;
  std::string::size_type loc, hloc;

  this->Authority = "";
  this->SceneID = "";
  this->NodeID = "";
  
  // check that the filename starts with the slicer3 scheme
  loc = fname.find("slicer:");
  if (loc != std::string::npos && (loc == 0))
    {
    this->Scheme = std::string(fname.begin(),
                               fname.begin() + std::string("slicer").size());
    loc = this->Scheme.size() + 1; // skip the colon
    
    // now check whether we have a local or remote resource
    if (std::string(fname.begin()+loc,
                    fname.begin()+loc+2) == "//")
      {
      // remote access, pull out a hostname
      hloc = fname.find("/", loc+2);
      if (hloc == std::string::npos)
        {
        // no hostname specified
        return 0;
        }
      this->Authority = std::string(fname.begin()+loc+2, fname.begin()+hloc);
      loc = hloc+1; // skip the slash
      }

    // now pull off the scene
    vtkMRMLScene *scene;
    hloc = fname.find("#", loc);
    if (hloc == std::string::npos)
      {
      // no scene specified
      return 0;
      }
    if (hloc >= loc)
      {
      this->SceneID = std::string(fname.begin()+loc, fname.begin()+hloc);
      
      sscanf(this->SceneID.c_str(), "%p", &scene);

      if (!scene)
        {
        // not a valid scene pointer
        return 0;
        }
      }
    else
      {
      this->SceneID = "";
      }
    loc = hloc+1;   // skip the hash
    
    // now pull off the node
    this->NodeID = std::string(fname.begin()+loc, fname.end());
    
    // so far so good.  now lookup the node in the scene and see if we
    // can cast down to a MRMLVolumeNode
    //
    vtkMRMLNode *node = scene->GetNodeByID(this->NodeID.c_str());

    if (node)
      {
      vtkMRMLVolumeNode *vnode = vtkMRMLVolumeNode::SafeDownCast(node);
    
      if (vnode)
        {
        // we are indeed referencing a volume node
        return vnode;
        }
      }
    }
  
  return 0;
}


bool
MRMLIDImageIO
::CanReadFile(const char* filename)
{
  return this->IsAVolumeNode(filename);
}

void
MRMLIDImageIO
::ReadImageInformation()
{
  vtkMRMLVolumeNode *node;

  node = this->FileNameToVolumeNodePtr( m_FileName.c_str() );
  if (node)
    {
    // VTK is only 3D
    this->SetNumberOfDimensions(3);

    // Get spacing, origin and directions from node. The node keeps
    // these in RAS, ITK needs them in LPS.

    vtkMatrix4x4 *rasToIjk = vtkMatrix4x4::New();
    node->GetRASToIJKMatrix(rasToIjk);

    vtkMatrix4x4 *ijkToRas = vtkMatrix4x4::New();

    vtkMatrix4x4::Invert(rasToIjk, ijkToRas);
    ijkToRas->Transpose();

    m_Direction.resize(3);
    m_Origin.resize(3);
    m_Spacing.resize(3);

    int i, j;
    for (i=0; i<3; i++)
      {
      // normalize vectors
      m_Spacing[i] = 0;
      for (j=0; j<3; j++)
        {
        m_Spacing[i] += ijkToRas->GetElement(i,j)* ijkToRas->GetElement(i,j);
        }
      if (m_Spacing[i] == 0.0)
        {
        m_Spacing[i] = 1;
        }
      m_Spacing[i] = sqrt(m_Spacing[i]);
      }
    for ( i=0; i<3; i++)
      {
      for (j=0; j<3; j++)
        {
        ijkToRas->SetElement(i, j, ijkToRas->GetElement(i,j)/m_Spacing[i]);
        }
      }
    vtkMatrix4x4* rasToLps = vtkMatrix4x4::New();
    rasToLps->Identity();
    rasToLps->SetElement(0,0,-1);
    rasToLps->SetElement(1,1,-1);

    vtkMatrix4x4* ijkToLps = vtkMatrix4x4::New();
    vtkMatrix4x4::Multiply4x4(ijkToRas, rasToLps, ijkToLps);

    for ( i=0; i<3; i++) 
      {
      m_Origin[i] =  ijkToRas->GetElement(3,i);
      m_Direction[i].resize(3);
      for (j=0; j<3; j++)
        {
        m_Direction[j][i] =  ijkToLps->GetElement(j,i);
        }
      }
    m_Origin[0] *= -1;
    m_Origin[1] *= -1;

    if (node->GetImageData() == NULL)
      {
      itkWarningMacro("itkMRMLIDImageIO: ReadImageInformation: node image data is null.");
      return;
      }
    if (node->GetImageData()->GetDimensions() == NULL)
      {
      itkWarningMacro("itkMRMLIDImageIO: ReadImageInformation: node image data has null dimentions");
      return;
      }
    this->SetDimensions(0, node->GetImageData()->GetDimensions()[0]);
    this->SetDimensions(1, node->GetImageData()->GetDimensions()[1]);
    this->SetDimensions(2, node->GetImageData()->GetDimensions()[2]);
    
    // Number of components, PixelType
    if (vtkMRMLDiffusionTensorVolumeNode::SafeDownCast(node) == 0)
      {
      // Scalar, Diffusion Weighted, or Vector image
      this->SetNumberOfComponents(node->GetImageData()
                                  ->GetNumberOfScalarComponents());
      }
    else
      {
      // Tensor image
      this->SetNumberOfComponents(node->GetImageData()->GetPointData()->GetTensors()->GetNumberOfComponents());
      }

    // PixelType
    if (this->GetNumberOfComponents() == 1)
      {
      this->SetPixelType(SCALAR);
      }
    else if (vtkMRMLDiffusionTensorVolumeNode::SafeDownCast(node) != 0)
      {
      // tensor at each voxel
      this->SetPixelType(DIFFUSIONTENSOR3D);
      }
    else if (vtkMRMLDiffusionWeightedVolumeNode::SafeDownCast(node) != 0)
      {
      // raw DWI
      this->SetPixelType(VECTOR);
      }
    else if (vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node) != 0)
      {
      // derived data from a diffusion weighted image, e.g. Q-ball
      this->SetPixelType(VECTOR);
      }
    else
      {
      // everything else...
      // what should the mapping be for multi-component scalars?
      this->SetPixelType(VECTOR);
      }
      
    // ComponentType
    switch (node->GetImageData()->GetScalarType())
      {
      case VTK_FLOAT: this->SetComponentType(FLOAT); break;
      case VTK_DOUBLE: this->SetComponentType(DOUBLE); break;
      case VTK_INT: this->SetComponentType(INT); break;
      case VTK_UNSIGNED_INT: this->SetComponentType(UINT); break;
      case VTK_SHORT: this->SetComponentType(SHORT); break;
      case VTK_UNSIGNED_SHORT: this->SetComponentType(USHORT); break;
      case VTK_LONG: this->SetComponentType(LONG); break;
      case VTK_UNSIGNED_LONG: this->SetComponentType(ULONG); break;
      case VTK_CHAR: this->SetComponentType(CHAR); break;
      case VTK_UNSIGNED_CHAR: this->SetComponentType(UCHAR); break;
      default: itkWarningMacro("Unknown scalar type.");
        this->SetComponentType(UNKNOWNCOMPONENTTYPE);
        break;
      }

    // For diffusion data, we need to get the measurement frame,
    // diffusion gradients, and b-values
    if (vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node) != 0)
      {
      vtkMRMLDiffusionImageVolumeNode *di 
        = vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node);
      
      // throw the measurement frame into the meta-data dictionary
      // following NRRD conventions
      //
      MetaDataDictionary &thisDic = this->GetMetaDataDictionary();

      EncapsulateMetaData<std::string >(thisDic, "modality", "DWMRI");

      this->SetDTDictionaryValues(thisDic, di);
      }
    else if (vtkMRMLDiffusionWeightedVolumeNode::SafeDownCast(node) != 0)
      {
      vtkMRMLDiffusionWeightedVolumeNode *dw
        = vtkMRMLDiffusionWeightedVolumeNode::SafeDownCast(node);

      // throw the measurement frame, diffusion gradients, and bvalue
      // into the meta-data dictionary following NRRD conventions
      //
      MetaDataDictionary &thisDic = this->GetMetaDataDictionary();

      EncapsulateMetaData<std::string >(thisDic, "modality", "DWMRI");

      this->SetDWDictionaryValues(thisDic, dw);
      }

    // Cleanup
    rasToIjk->Delete();
    ijkToRas->Delete();
    rasToLps->Delete();
    ijkToLps->Delete();
    }    
}

// Read from the MRML scene
void
MRMLIDImageIO
::Read(void *buffer)
{
  vtkMRMLVolumeNode *node;

  node = this->FileNameToVolumeNodePtr( m_FileName.c_str() );
  if (node)
    {
    // buffer is preallocated, memcpy the data
    if (vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node) == 0)
      {
      // Scalar, Diffusion Weighted, or Vector image
      memcpy(buffer, node->GetImageData()->GetScalarPointer(), 
             this->GetImageSizeInBytes());
      }
    else
      {
      // Tensor image
      memcpy(buffer, 
             node->GetImageData()->GetPointData()->GetTensors()->GetVoidPointer(0), 
             this->GetImageSizeInBytes());
      }
    }
}

// Read from the MRML scene
bool
MRMLIDImageIO
::CanUseOwnBuffer()
{
  return true;
}

// Read from the MRML scene
void
MRMLIDImageIO
::ReadUsingOwnBuffer()
{
  return;
}



// Read from the MRML scene
void *
MRMLIDImageIO
::GetOwnBuffer()
{
  vtkMRMLVolumeNode *node;

  node = this->FileNameToVolumeNodePtr( m_FileName.c_str() );
  if (node)
    {
    // buffer is preallocated, memcpy the data
    if (vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node) == 0)
      {
      // Scalar, Diffusion Weighted, or Vector image
      return node->GetImageData()->GetScalarPointer();
      }
    else
      {
      return node->GetImageData()->GetPointData()->GetTensors()->GetVoidPointer(0);
      }
    }

  throw( "MRML Node does not contain image data." );

  return static_cast< void * >( 0 );
}

bool
MRMLIDImageIO
::CanWriteFile(const char* filename)
{
  return this->IsAVolumeNode(filename);
}

// Write to the MRML scene

void
MRMLIDImageIO
::WriteImageInformation()
{
}


void
MRMLIDImageIO
::WriteImageInformation(vtkMRMLVolumeNode *node, vtkImageData *img)
{
  unsigned int i, j;
  
  vtkMatrix4x4* ijkToLps = vtkMatrix4x4::New();
  vtkMatrix4x4* rasToIjk = vtkMatrix4x4::New();
  vtkMatrix4x4* lpsToRas = vtkMatrix4x4::New();
  
  rasToIjk->Identity();
  ijkToLps->Identity();
  lpsToRas->Identity();

  for (i = 0; (i < this->GetNumberOfDimensions()) && (i < 3); i++)
    {
    // Get IJK to RAS direction vector
    for ( j=0; (j < this->GetNumberOfDimensions()) && (j < 3); j++ )
      {
      ijkToLps->SetElement(j, i, m_Spacing[i]*this->GetDirection(i)[j]);
      }
    }
    
  // Transform from LPS to RAS
  lpsToRas->SetElement(0,0,-1);
  lpsToRas->SetElement(1,1,-1);

  vtkMatrix4x4::Multiply4x4(lpsToRas,ijkToLps, rasToIjk);

  for (j = 0; (j < this->GetNumberOfDimensions()) && (j < 3); j++)
    {
    if (j < 2)
      {
      rasToIjk->SetElement(j, 3, -m_Origin[j]);
      }
    else
      {
      rasToIjk->SetElement(j, 3, m_Origin[j]);
      }
    }
  rasToIjk->Invert();
  
  rasToIjk->SetElement(3,3,1.0);
  node->SetRASToIJKMatrix(rasToIjk);

  // Fill in dimensions
  // VTK is only 3D, only copy the first 3 dimensions, fill in with
  // reasonable defaults for the rest
  if (this->GetNumberOfDimensions() > 3)
    {
    itkWarningMacro("Dimension of image is too high for VTK (Dimension = "
                    << this->GetNumberOfDimensions() << ")" );
    }
  
  int dim[3];
  double origin[3];
  double spacing[3];
  
  for (i=0; (i < this->GetNumberOfDimensions()) && (i < 3); ++i)
    {
    dim[i] = this->GetDimensions(i);
    origin[i] = 0.0;
    spacing[i] = 1.0;
    }
  if (this->GetNumberOfDimensions() < 3)
    {
    // VTK is only 3D, fill in remaining dimensions
    for (; i < 3; ++i)
      {
      dim[i] = 1;
      origin[i] = 0.0;
      spacing[i] = 1.0;
      }
    }
  img->SetDimensions(dim);
  img->SetOrigin(origin);
  img->SetSpacing(spacing);

  // ComponentType
  switch (this->GetComponentType())
    {
    case FLOAT: img->SetScalarTypeToFloat(); break;
    case DOUBLE: img->SetScalarTypeToDouble(); break;
    case INT: img->SetScalarTypeToInt(); break;
    case UINT: img->SetScalarTypeToUnsignedInt(); break;
    case SHORT: img->SetScalarTypeToShort(); break;
    case USHORT: img->SetScalarTypeToUnsignedShort(); break;
    case LONG: img->SetScalarTypeToLong(); break;
    case ULONG: img->SetScalarTypeToUnsignedLong(); break;
    case CHAR: img->SetScalarTypeToChar(); break;
    case UCHAR: img->SetScalarTypeToUnsignedChar(); break;
    default:
      // What should we do?
      itkWarningMacro("Unknown scalar type.");
      img->SetScalarTypeToShort();
      break;
    }

  // Number of components, PixelType
  if (vtkMRMLDiffusionTensorVolumeNode::SafeDownCast(node) == 0)
    {
    // Scalar, Diffusion Weighted, or Vector image
    img->SetNumberOfScalarComponents(this->GetNumberOfComponents());
    }
  else
    {
    vtkDataArray *tensors = vtkDataArray::CreateDataArray(img->GetScalarType());

    // number of components in VTK is 9 for tensors but for ITK it is 6
    // NOTE: VTK requires that the number of components be set on a
    // data array before you are allowed to set it as the tensors!!!!
    // NOTE: Tensors need be the "named" or the AssignAttributes
    // filter will crash. Doesn't matter what the name is, just needs
    // to have one.
    tensors->SetNumberOfComponents(9);
    tensors->SetName("Tensors");
    img->GetPointData()->SetTensors( tensors );
    tensors->Delete();
    }

  // For diffusion data, we need to set the measurement frame,
  // diffusion gradients, and bvalues from values in the MetaDataDictionary
  if (vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node) != 0)
    {
    vtkMRMLDiffusionImageVolumeNode *di 
      = vtkMRMLDiffusionImageVolumeNode::SafeDownCast(node);
    
    MetaDataDictionary &thisDic = this->GetMetaDataDictionary();
    
    this->SetDTNodeValues(di, thisDic);
    }
  else if (vtkMRMLDiffusionWeightedVolumeNode::SafeDownCast(node) != 0)
    {
    vtkMRMLDiffusionWeightedVolumeNode *dw
      = vtkMRMLDiffusionWeightedVolumeNode::SafeDownCast(node);
    
    MetaDataDictionary &thisDic = this->GetMetaDataDictionary();
    
    this->SetDWNodeValues(dw, thisDic);
    }

  
  // Cleanup
  lpsToRas->Delete();
  ijkToLps->Delete();
  rasToIjk->Delete();
}

// Write to the MRML scene

void
MRMLIDImageIO
::Write(const void *buffer)
{
  vtkMRMLVolumeNode *node;

  node = this->FileNameToVolumeNodePtr( m_FileName.c_str() );
  if (node)
    {
    // Don't send Modified events
    //
    node->DisableModifiedEventOn();
    
    // Need to create a VTK ImageData to hang off the node if there is
    // not one already there
    //
    vtkImageData *img = 0;
    img = node->GetImageData();
    if (!img)
      {
      img = vtkImageData::New();
      node->SetAndObserveImageData(img);
      img->Delete();
      }

    // Disconnect the observers from the image
    //
    //
    img->Register(NULL);  // keep a handle
    node->SetAndObserveImageData(NULL);

    // Configure the information on the node/image data
    //
    //
    this->WriteImageInformation(node, img);

    // Allocate the data, copy the data 
    //
    //
    if (vtkMRMLDiffusionTensorVolumeNode::SafeDownCast(node) == 0)
      {
      // Everything but tensor images are passed in the scalars
      img->AllocateScalars();

      memcpy(img->GetScalarPointer(), buffer,
             img->GetPointData()->GetScalars()->GetNumberOfComponents() *
             img->GetPointData()->GetScalars()->GetNumberOfTuples() *
             img->GetPointData()->GetScalars()->GetDataTypeSize()
        );
      }
    else
      {
      // Tensor image
      unsigned long imagesizeinpixels = this->GetImageSizeInPixels();

      // Allocate tensor image (number of components set in WriteInformation())
      img->GetPointData()->GetTensors()->SetNumberOfTuples(imagesizeinpixels);

      // Tensors comming from ITK will be 6 components.  Need to
      // convert to 9 components for VTK
      void *mptr = img->GetPointData()->GetTensors()->GetVoidPointer(0);
      const void *bptr = buffer;

      short csize =img->GetPointData()->GetTensors()->GetDataTypeSize();
      for (unsigned long i=0; i < imagesizeinpixels; ++i)
        {
        memcpy((char*)mptr, (char*)bptr, csize);
        memcpy((char*)mptr+csize, (char*)bptr+csize, csize);
        memcpy((char*)mptr+2*csize, (char*)bptr+2*csize, csize);
        memcpy((char*)mptr+3*csize, (char*)bptr+1*csize, csize);
        memcpy((char*)mptr+4*csize, (char*)bptr+3*csize, csize);
        memcpy((char*)mptr+5*csize, (char*)bptr+4*csize, csize);
        memcpy((char*)mptr+6*csize, (char*)bptr+2*csize, csize);
        memcpy((char*)mptr+7*csize, (char*)bptr+4*csize, csize);
        memcpy((char*)mptr+8*csize, (char*)bptr+5*csize, csize);
        mptr = (char*)mptr + 9*csize;
        bptr = (char*)bptr + 6*csize;
        }
      }

    // Connect the observers to the image
    node->SetAndObserveImageData( img );
    img->UnRegister(NULL); // release the handle

    // Enable Modified events
    //
    node->DisableModifiedEventOff();
    }
}


void
MRMLIDImageIO
::PrintSelf(std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf(os, indent);

  os << indent << "Scheme: " << this->Scheme << std::endl;
  os << indent << "Authority: " << this->Authority << std::endl;
  os << indent << "SceneID: " << this->SceneID << std::endl;
  os << indent << "NodeID: " << this->NodeID << std::endl;
  
}

void
MRMLIDImageIO
::SetDWDictionaryValues(MetaDataDictionary &dict, 
                        vtkMRMLDiffusionWeightedVolumeNode *dw)
{
  // Measurement frame
  std::string measurementFrameKey = "NRRD_measurement frame";
  double measurementFrame[3][3];
  std::vector<std::vector<double> > measurementFrameValue(3);

  dw->GetMeasurementFrameMatrix(measurementFrame);

//    std::cout << "Sending measurement frame" << std::endl;
//    for (unsigned int i=0; i < 3; i++)
//      {
//      for (unsigned int j=0; j < 3; j++)
//        {
//        std::cout << measurementFrame[i][j] << " ";
//        }
//      std::cout << std::endl;
//      }

  // copy into something we can serialize, e.g. something with a
  // copy constructor. 
  double rasToLPS[] = {-1.0, -1.0, 1.0};
  for (unsigned int i=0; i < 3; i++)
    {
    measurementFrameValue[i].resize(3);
    for (unsigned int j=0; j < 3; j++)
      {
      measurementFrameValue[i][j] = rasToLPS[j]*measurementFrame[j][i];
      }
    }

  EncapsulateMetaData<std::vector<std::vector<double> > >(dict,
                                           measurementFrameKey,
                                         measurementFrameValue);

  // B value, encapsulate as a string like itkNRRDImageIO would do so
  // that programs can be written the same when using itkNRRDImageIO
  // or itkMRMLIDImageIO
  std::string bValueKey = "DWMRI_b-value";
  double maxBValue = itk::NumericTraits<double>::NonpositiveMin();
  for (int i=0; i < dw->GetNumberOfGradients(); ++i)
    {
    if (dw->GetBValue(i) > maxBValue)
      {
      maxBValue = dw->GetBValue(i);
      }
    }
      
  // EncapsulateMetaData<double>(dict, bValueKey, maxBValue);
  std::stringstream bvaluess;
  bvaluess << maxBValue;
  EncapsulateMetaData<std::string>(dict, bValueKey, bvaluess.str());

  // Diffusion gradients, encapsulate as a string like itkNRRDImageIO
  // would do so that programs can be written the same when using
  // itkNRRDImageIO or itkMRMLIDImageIO
  std::string diffusionGradientKey = "DWMRI_gradient_";
      
  for (int i=0; i < dw->GetNumberOfGradients(); ++i)
    {
    std::vector<double> gradient(3);
    double *g;

    g = dw->GetDiffusionGradient(i);

    for (unsigned int j=0; j < 3; ++j)
      {
//      gradient[j] = g[j] * dw->GetBValue(i) / maxBValue;
        gradient[j] = g[j];
      }

    // format the key so gradients appear in dictionary in sorted order
    std::stringstream ss;
    ss << diffusionGradientKey;
    ss.fill('0');
    ss.width(4);
    ss << i << endl;

    // EncapsulateMetaData<std::vector<double> >(dict, ss.str(), gradient);
    std::stringstream gradientss;
    gradientss << gradient[0] << " " << gradient[1] << " " << gradient[2];
    EncapsulateMetaData<std::string>(dict, ss.str(),gradientss.str());
    }
}

void
MRMLIDImageIO
::SetDWNodeValues(vtkMRMLDiffusionWeightedVolumeNode *dw, 
                  MetaDataDictionary &dict)
{
  // Measurement frame
  std::string measurementFrameKey = "NRRD_measurement frame";
  double measurementFrame[3][3];
  std::vector<std::vector<double> > measurementFrameValue(3);

  for (unsigned int i=0; i < 3; i++)
    {
    measurementFrameValue[i].resize(3);
    }
  
  ExposeMetaData<std::vector<std::vector<double> > >(dict,
                                                     measurementFrameKey,
                                                     measurementFrameValue);

  // convert from a dictionary value to a data format for the node
  double lpsToRAS[] = {-1.0, -1.0, 1.0};
  for (unsigned int i=0; i < 3; i++)
    {
    for (unsigned int j=0; j < 3; j++)
      {
      measurementFrame[i][j] = lpsToRAS[i] * measurementFrameValue[j][i];
      }
    }

  dw->SetMeasurementFrameMatrix(measurementFrame);

//    std::cout << "Received measurement frame" << std::endl;
//    for (unsigned int i=0; i < 3; i++)
//      {
//      for (unsigned int j=0; j < 3; j++)
//        {
//        std::cout << measurementFrame[i][j] << " ";
//        }
//      std::cout << std::endl;
//      }


  // B value, just get it from the dictionary for now
  // B value is stored as a string in the MetaDataDictionary in
  // MRMLIDImageIO, just like it is for NRRDImageIO
  std::string bValueKey = "DWMRI_b-value";
  double maxBValue = 1.0;
      
  // ExposeMetaData<double>(dict, bValueKey, maxBValue);
  std::string bValueString;
  ExposeMetaData<std::string>(dict, bValueKey, bValueString);  
  std::stringstream bValueSS;
  bValueSS << bValueString;
  bValueSS >> maxBValue;

  // Get the gradients from the dictionary, compute their lengths to
  // get b-value / max_b-value, then normalize the gradients

  // Diffusion gradients are stored as strings in the
  // MetaDataDictionary in MRMLIDImageIO, just like for NRRDImageIO
  std::string diffusionGradientKey = "DWMRI_gradient_";

  // Gradients are keyed as DWMRI_gradient_0001, DWMRI_gradient_0002, etc.
  std::vector<std::string> keys = dict.GetKeys();

  std::vector<double> bvalues;
  std::vector<std::vector<double> > gradients;
  for (std::vector<std::string>::iterator it = keys.begin();
       it != keys.end(); ++it)
    {
    if ((*it).find(diffusionGradientKey) != std::string::npos)
      {
      // found a gradient
      std::string gradientString;
      std::vector<double> gradient(3);
      // ExposeMetaData<std::vector<double> >(dict, *it, gradient);
      ExposeMetaData<std::string>(dict, *it, gradientString);

      std::stringstream gradientSS;
      gradientSS << gradientString;
      gradientSS >> gradient[0];
      gradientSS >> gradient[1];
      gradientSS >> gradient[2];
/*
      // gradient length is the b-value / max_b-value
      double sum = 0.0;
      for (unsigned int i=0; i < 3; ++i)
        {
        sum += (gradient[i] * gradient[i]);
        }
      sum = sqrt(sum);
      bvalues.push_back( sum * maxBValue );

      if (sum > 0)
        for (unsigned int i=0; i < 3; ++i)
          {
            gradient[i] /= sum;
          }
*/
      gradients.push_back(gradient);
      }
    }

  // convert gradients
  dw->SetNumberOfGradients( gradients.size() );
  for (unsigned int i=0; i < gradients.size(); ++i)
    {
    dw->SetBValue(i, bvalues[i]);
    dw->SetDiffusionGradient(i, &gradients[i][0]);
    }
}





void
MRMLIDImageIO
::SetDTDictionaryValues(MetaDataDictionary &dict, 
                        vtkMRMLDiffusionImageVolumeNode *di)
{
  // Measurement frame
  std::string measurementFrameKey = "NRRD_measurement frame";
  double measurementFrame[3][3];
  std::vector<std::vector<double> > measurementFrameValue(3);

  di->GetMeasurementFrameMatrix(measurementFrame);

//    std::cout << "Sending measurement frame" << std::endl;
//    for (unsigned int i=0; i < 3; i++)
//      {
//      for (unsigned int j=0; j < 3; j++)
//        {
//        std::cout << measurementFrame[i][j] << " ";
//        }
//      std::cout << std::endl;
//      }

  // copy into something we can serialize, e.g. something with a
  // copy constructor. 
  double rasToLPS[] = {-1.0, -1.0, 1.0};
  for (unsigned int i=0; i < 3; i++)
    {
    measurementFrameValue[i].resize(3);
    for (unsigned int j=0; j < 3; j++)
      {
      measurementFrameValue[i][j] = rasToLPS[j]*measurementFrame[j][i];
      }
    }

  EncapsulateMetaData<std::vector<std::vector<double> > >(dict,
                                           measurementFrameKey,
                                         measurementFrameValue);
}

void
MRMLIDImageIO
::SetDTNodeValues(vtkMRMLDiffusionImageVolumeNode *di, 
                  MetaDataDictionary &dict)
{
  // Measurement frame
  std::string measurementFrameKey = "NRRD_measurement frame";
  double measurementFrame[3][3];
  std::vector<std::vector<double> > measurementFrameValue(3);

  for (unsigned int i=0; i < 3; i++)
    {
    measurementFrameValue[i].resize(3);
    }
  
  ExposeMetaData<std::vector<std::vector<double> > >(dict,
                                                     measurementFrameKey,
                                                     measurementFrameValue);

  // convert from a dictionary value to a data format for the node
  double lpsToRAS[] = {-1.0, -1.0, 1.0};
  for (unsigned int i=0; i < 3; i++)
    {
    for (unsigned int j=0; j < 3; j++)
      {
      measurementFrame[i][j] = lpsToRAS[i] * measurementFrameValue[j][i];
      }
    }

  di->SetMeasurementFrameMatrix(measurementFrame);

//    std::cout << "Received measurement frame" << std::endl;
//    for (unsigned int i=0; i < 3; i++)
//      {
//      for (unsigned int j=0; j < 3; j++)
//        {
//        std::cout << measurementFrame[i][j] << " ";
//        }
//      std::cout << std::endl;
//      }
}

} // end namespace itk
