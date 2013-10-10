// CIS565 CUDA Raytracer: A parallel raytracer for Patrick Cozzi's CIS565: GPU Computing at the University of Pennsylvania
// Written by Yining Karl Li, Copyright (c) 2012 University of Pennsylvania
// This file includes code from:
//       Yining Karl Li's TAKUA Render, a massively parallel pathtracing renderer: http://www.yiningkarlli.com
// Edited by Liam Boone for use with CUDA v5.5

#include <iostream>
#include "scene.h"
#include <cstring>

scene::scene(string filename){
    cout << "Reading scene from " << filename << " ..." << endl;
    cout << " " << endl;
    char* fname = (char*)filename.c_str();
    fp_in.open(fname);
    if(fp_in.is_open()){
        while(fp_in.good()){
            string line;
            utilityCore::safeGetline(fp_in,line);
            if(!line.empty()){
                vector<string> tokens = utilityCore::tokenizeString(line);
                if(strcmp(tokens[0].c_str(), "MATERIAL")==0){
                    loadMaterial(tokens[1]);
                    cout << " " << endl;
                }else if(strcmp(tokens[0].c_str(), "OBJECT")==0){
                    loadObject(tokens[1]);
                    cout << " " << endl;
                }else if(strcmp(tokens[0].c_str(), "CAMERA")==0){
                    loadCamera();
                    cout << " " << endl;
                }
            }
        }
    }
}

int scene::loadObject(string objectid){
    int id = atoi(objectid.c_str());
    if (id!=objects.size()){
        cout << "ERROR: OBJECT ID does not match expected number of objects" << endl;
        return -1;
    } else{
        cout << "Loading Object " << id << "..." << endl;
        geom newObject;
        string line;
        
        //load object type 
        utilityCore::safeGetline(fp_in,line);
        if (!line.empty() && fp_in.good()){
            if(strcmp(line.c_str(), "sphere")==0){
                cout << "Creating new sphere..." << endl;
                newObject.type = SPHERE;
            }else if(strcmp(line.c_str(), "cube")==0){
                cout << "Creating new cube..." << endl;
                newObject.type = CUBE;
            }else{
                string objline = line;
                string name;
                string extension;
                istringstream liness(objline);
                getline(liness, name, '.');
                getline(liness, extension, '.');
                if(strcmp(extension.c_str(), "obj")==0){
                    cout << "Creating new mesh..." << endl;
                    cout << "Reading mesh from " << line << "... " << endl;
                    newObject.type = MESH;
                } else{
                    cout << "ERROR: " << line << " is not a valid object type!" << endl;
                    return -1;
                }
            }
        }
       
    //link material
    utilityCore::safeGetline(fp_in,line);
    if(!line.empty() && fp_in.good()){
        vector<string> tokens = utilityCore::tokenizeString(line);
        newObject.materialid = atoi(tokens[1].c_str());
        cout << "Connecting Object " << objectid << " to Material " << newObject.materialid << "..." << endl;
    }
        
    //load scene
    for(int i=0; i<3; i++) {
        utilityCore::safeGetline(fp_in,line);
        vector<string> tokens = utilityCore::tokenizeString(line);
        if (strcmp(tokens[0].c_str(), "TRANS")==0) {
            newObject.translation = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        } else if(strcmp(tokens[0].c_str(), "ROTAT")==0) {
            newObject.rotation = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        } else if(strcmp(tokens[0].c_str(), "SCALE")==0) {
            newObject.scale = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        }
    }
    utilityCore::safeGetline(fp_in,line);
    
    glm::mat4 transform = utilityCore::buildTransformationMatrix(newObject.translation, newObject.rotation, newObject.scale);
    newObject.transform = utilityCore::glmMat4ToCudaMat4(transform);
    newObject.inverseTransform = utilityCore::glmMat4ToCudaMat4(glm::inverse(transform));
    
    objects.push_back(newObject);
    
    cout << "Loaded Object " << objectid << "!" << endl;
        return 1;
    }
}

int scene::loadCamera() {
    cout << "Loading Camera ..." << endl;
        camera newCamera;
    float fovy;
    
    //load camera properties
    string line;
    for(int i=0; i<7; i++) {
        utilityCore::safeGetline(fp_in,line);
        vector<string> tokens = utilityCore::tokenizeString(line);
        if (strcmp(tokens[0].c_str(), "RES")==0) {
            newCamera.resolution = glm::vec2(atoi(tokens[1].c_str()), atoi(tokens[2].c_str()));
        } else if (strcmp(tokens[0].c_str(), "FOVY")==0) {
            fovy = atof(tokens[1].c_str());
        } else if (strcmp(tokens[0].c_str(), "ITERATIONS")==0) {
            newCamera.iterations = atoi(tokens[1].c_str());
        } else if (strcmp(tokens[0].c_str(), "FILE")==0) {
            newCamera.imageName = tokens[1];
        } else if (strcmp(tokens[0].c_str(), "EYE")==0) {
            newCamera.position = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        } else if (strcmp(tokens[0].c_str(), "VIEW")==0) {
            newCamera.view = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        } else if (strcmp(tokens[0].c_str(), "UP")==0) {
            newCamera.up = glm::vec3(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
        }
    }
    utilityCore::safeGetline(fp_in,line);

    //calculate fov based on resolution
    float yscaled = tan(fovy*(PI/180));
    float xscaled = (yscaled * newCamera.resolution.x)/newCamera.resolution.y;
    float fovx = (atan(xscaled)*180)/PI;
    newCamera.fov = glm::vec2(fovx, fovy);

    renderCam = newCamera;
    
    //set up render camera stuff
    renderCam.image = new glm::vec3[(int)renderCam.resolution.x*(int)renderCam.resolution.y];
    renderCam.rayList = new ray[(int)renderCam.resolution.x*(int)renderCam.resolution.y];
    for(int i=0; i<renderCam.resolution.x*renderCam.resolution.y; i++){
        renderCam.image[i] = glm::vec3(0,0,0);
    }
    
    cout << "Loaded camera!" << endl;
    return 1;
}

int scene::loadMaterial(string materialid) {
    int id = atoi(materialid.c_str());
    if (id!=materials.size()) {
        cout << "ERROR: MATERIAL ID does not match expected number of materials" << endl;
        return -1;
    } else {
        cout << "Loading Material " << id << "..." << endl;
        material newMaterial;
    
        //load static properties
        for(int i=0; i<10; i++) {
            string line;
            utilityCore::safeGetline(fp_in,line);
            vector<string> tokens = utilityCore::tokenizeString(line);
            if(strcmp(tokens[0].c_str(), "RGB")==0){
                glm::vec3 color( atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()) );
                newMaterial.color = color;
            } else if(strcmp(tokens[0].c_str(), "SPECEX")==0) {
                newMaterial.specularExponent = atof(tokens[1].c_str());                  
            } else if(strcmp(tokens[0].c_str(), "SPECRGB")==0) {
                glm::vec3 specColor( atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()) );
                newMaterial.specularColor = specColor;
            } else if(strcmp(tokens[0].c_str(), "REFL")==0) {
                newMaterial.hasReflective = atof(tokens[1].c_str());
            } else if(strcmp(tokens[0].c_str(), "REFR")==0) {
                newMaterial.hasRefractive = atof(tokens[1].c_str());
            } else if(strcmp(tokens[0].c_str(), "REFRIOR")==0) {
                newMaterial.indexOfRefraction = atof(tokens[1].c_str());                      
            } else if(strcmp(tokens[0].c_str(), "SCATTER")==0) {
                newMaterial.hasScatter = atof(tokens[1].c_str());
            } else if(strcmp(tokens[0].c_str(), "ABSCOEFF")==0) {
                glm::vec3 abscoeff(atof(tokens[1].c_str()), atof(tokens[2].c_str()), atof(tokens[3].c_str()));
                newMaterial.absorptionCoefficient = abscoeff;
            } else if(strcmp(tokens[0].c_str(), "RSCTCOEFF")==0) {
                newMaterial.reducedScatterCoefficient = atof(tokens[1].c_str());                      
            } else if(strcmp(tokens[0].c_str(), "EMITTANCE")==0) {
                newMaterial.emittance = atof(tokens[1].c_str());                      
            
            }
        }
        materials.push_back(newMaterial);
        return 1;
    }
}
