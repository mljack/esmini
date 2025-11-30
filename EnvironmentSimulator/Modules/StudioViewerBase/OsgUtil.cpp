/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#include "OsgUtil.hpp"
#include "XmlUtil.hpp"
#include "CommonMini.hpp"

#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Vec3>
#include <osg/Material>
#include <osgText/Text>
#include <osg/LineWidth>

osg::ref_ptr<osg::Node> CreateGreenSphereGeometry(double radius, int latitudeBands, int longitudeBands)
{
    // Create geometry for a sphere
    osg::ref_ptr<osg::Geode>    sphereGeode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry> sphereGeometry = new osg::Geometry();

    // Create vertices
    osg::ref_ptr<osg::Vec3Array> vertices  = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals   = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array();

    // Generate sphere vertices and normals
    for (int lat = 0; lat <= latitudeBands; lat++)
    {
        double theta    = lat * M_PI / latitudeBands;
        double sinTheta = sin(theta);
        double cosTheta = cos(theta);

        for (int lon = 0; lon <= longitudeBands; lon++)
        {
            double phi    = lon * 2 * M_PI / longitudeBands;
            double sinPhi = sin(phi);
            double cosPhi = cos(phi);

            // Calculate vertex position
            double x = cosPhi * sinTheta;
            double y = cosTheta;
            double z = sinPhi * sinTheta;

            vertices->push_back(osg::Vec3(x * radius, y * radius, z * radius));
            normals->push_back(osg::Vec3(x, y, z));
            texcoords->push_back(osg::Vec2((double)lon / longitudeBands, (double)lat / latitudeBands));
        }
    }

    // Create indices for triangles
    std::vector<unsigned int> indicesVec;

    for (int lat = 0; lat < latitudeBands; lat++)
    {
        for (int lon = 0; lon < longitudeBands; lon++)
        {
            int first  = (lat * (longitudeBands + 1)) + lon;
            int second = first + longitudeBands + 1;

            // First triangle
            indicesVec.push_back(first);
            indicesVec.push_back(second);
            indicesVec.push_back(first + 1);

            // Second triangle
            indicesVec.push_back(second);
            indicesVec.push_back(second + 1);
            indicesVec.push_back(first + 1);
        }
    }

    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES, indicesVec.size());
    for (unsigned int i = 0; i < indicesVec.size(); i++)
    {
        (*indices)[i] = indicesVec[i];
    }

    // Set up the geometry
    sphereGeometry->setVertexArray(vertices.get());
    sphereGeometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
    sphereGeometry->setTexCoordArray(0, texcoords.get());
    sphereGeometry->getPrimitiveSetList().push_back(indices.get());

    // Enable lighting
    sphereGeometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);

    // Create material for green color
    osg::ref_ptr<osg::Material> material = new osg::Material();
    material->setAmbient(osg::Material::FRONT, osg::Vec4(0.0f, 0.8f, 0.0f, 1.0f));
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    material->setSpecular(osg::Material::FRONT, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
    material->setShininess(osg::Material::FRONT, 12.0f);

    sphereGeometry->getOrCreateStateSet()->setAttributeAndModes(material, osg::StateAttribute::ON);

    // Add geometry to geode
    sphereGeode->addDrawable(sphereGeometry);

    return sphereGeode;
}

osg::ref_ptr<osg::Node> CreateBlueBoxGeometry(double width, double height, double depth)
{
    // Create geometry for a box
    osg::ref_ptr<osg::Geode>    boxGeode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry> boxGeometry = new osg::Geometry();

    // Create vertices for a box centered at origin
    osg::ref_ptr<osg::Vec3Array> vertices  = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals   = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array();

    double w = width / 2.0;
    double h = height / 2.0;
    double d = depth / 2.0;

    // Box vertices (8 corners)
    osg::Vec3 corners[] = {
        osg::Vec3(-w, -h, -d),  // 0
        osg::Vec3(w, -h, -d),   // 1
        osg::Vec3(w, h, -d),    // 2
        osg::Vec3(-w, h, -d),   // 3
        osg::Vec3(-w, -h, d),   // 4
        osg::Vec3(w, -h, d),    // 5
        osg::Vec3(w, h, d),     // 6
        osg::Vec3(-w, h, d)     // 7
    };

    // Box faces (6 faces, each with 4 vertices)
    int faces[][4] = {
        {0, 1, 2, 3},  // back
        {4, 5, 6, 7},  // front
        {0, 4, 5, 1},  // bottom
        {3, 2, 6, 7},  // top
        {1, 5, 6, 2},  // right
        {0, 3, 7, 4}   // left
    };

    // Box face normals
    osg::Vec3 faceNormals[] = {
        osg::Vec3(0, 0, -1),  // back
        osg::Vec3(0, 0, 1),   // front
        osg::Vec3(0, -1, 0),  // bottom
        osg::Vec3(0, 1, 0),   // top
        osg::Vec3(1, 0, 0),   // right
        osg::Vec3(-1, 0, 0)   // left
    };

    // Add vertices, normals and texture coordinates for each face
    for (int face = 0; face < 6; face++)
    {
        for (int i = 0; i < 4; i++)
        {
            vertices->push_back(corners[faces[face][i]]);
            normals->push_back(faceNormals[face]);
            texcoords->push_back(osg::Vec2(static_cast<float>(i) / 3.0f, static_cast<float>(face) / 5.0f));
        }
    }

    // Create indices for triangles (2 triangles per face)
    std::vector<unsigned int> indicesVec;
    for (int face = 0; face < 6; face++)
    {
        int base = face * 4;
        // First triangle
        indicesVec.push_back(base);
        indicesVec.push_back(base + 1);
        indicesVec.push_back(base + 2);
        // Second triangle
        indicesVec.push_back(base);
        indicesVec.push_back(base + 2);
        indicesVec.push_back(base + 3);
    }

    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES, indicesVec.size());
    for (unsigned int i = 0; i < indicesVec.size(); i++)
    {
        (*indices)[i] = indicesVec[i];
    }

    // Set up the geometry
    boxGeometry->setVertexArray(vertices.get());
    boxGeometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
    boxGeometry->setTexCoordArray(0, texcoords.get());
    boxGeometry->getPrimitiveSetList().push_back(indices.get());

    // Enable lighting
    boxGeometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);

    // Create material for blue color
    osg::ref_ptr<osg::Material> material = new osg::Material();
    material->setAmbient(osg::Material::FRONT, osg::Vec4(0.0f, 0.0f, 0.8f, 1.0f));
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(0.2f, 0.2f, 1.0f, 1.0f));
    material->setSpecular(osg::Material::FRONT, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
    material->setShininess(osg::Material::FRONT, 12.0f);

    boxGeometry->getOrCreateStateSet()->setAttributeAndModes(material, osg::StateAttribute::ON);

    // Add geometry to geode
    boxGeode->addDrawable(boxGeometry);

    return boxGeode;
}

osg::ref_ptr<osg::Node> CreateRedCylinderGeometry(double radius, double height, int segments)
{
    // Create geometry for a cylinder
    osg::ref_ptr<osg::Geode>    cylinderGeode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry> cylinderGeometry = new osg::Geometry();

    // Create vertices
    osg::ref_ptr<osg::Vec3Array> vertices  = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals   = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array();

    double h = height / 2.0;

    // Bottom center vertex
    vertices->push_back(osg::Vec3(0.0, 0.0, -h));
    normals->push_back(osg::Vec3(0.0, 0.0, -1.0));
    texcoords->push_back(osg::Vec2(0.5f, 0.5f));

    // Bottom circle vertices
    for (int i = 0; i <= segments; i++)
    {
        double angle = 2.0 * M_PI * i / segments;
        double x     = radius * cos(angle);
        double y     = radius * sin(angle);

        vertices->push_back(osg::Vec3(x, y, -h));
        normals->push_back(osg::Vec3(0.0, 0.0, -1.0));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 0.0f));
    }

    // Top center vertex
    vertices->push_back(osg::Vec3(0.0, 0.0, h));
    normals->push_back(osg::Vec3(0.0, 0.0, 1.0));
    texcoords->push_back(osg::Vec2(0.5f, 0.5f));

    // Top circle vertices
    for (int i = 0; i <= segments; i++)
    {
        double angle = 2.0 * M_PI * i / segments;
        double x     = radius * cos(angle);
        double y     = radius * sin(angle);

        vertices->push_back(osg::Vec3(x, y, h));
        normals->push_back(osg::Vec3(0.0, 0.0, 1.0));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 1.0f));
    }

    // Side vertices
    for (int i = 0; i <= segments; i++)
    {
        double angle = 2.0 * M_PI * i / segments;
        double x     = radius * cos(angle);
        double y     = radius * sin(angle);

        // Bottom side vertex
        vertices->push_back(osg::Vec3(x, y, -h));
        // Normal points outward
        normals->push_back(osg::Vec3(cos(angle), sin(angle), 0.0));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 0.0f));

        // Top side vertex
        vertices->push_back(osg::Vec3(x, y, h));
        normals->push_back(osg::Vec3(cos(angle), sin(angle), 0.0));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 1.0f));
    }

    // Create indices for triangles
    std::vector<unsigned int> indicesVec;

    // Bottom cap
    for (int i = 1; i < segments; i++)
    {
        indicesVec.push_back(0);  // center
        indicesVec.push_back(i);
        indicesVec.push_back(i + 1);
    }

    // Top cap
    int topCenterIndex = segments + 2;
    for (int i = 1; i < segments; i++)
    {
        indicesVec.push_back(topCenterIndex);
        indicesVec.push_back(topCenterIndex + i + 1);
        indicesVec.push_back(topCenterIndex + i);
    }

    // Sides
    int sideStartIndex = 2 * (segments + 1) + 2;
    for (int i = 0; i < segments; i++)
    {
        int bottom     = sideStartIndex + i * 2;
        int top        = bottom + 1;
        int nextBottom = sideStartIndex + ((i + 1) % segments) * 2;
        int nextTop    = nextBottom + 1;

        // First triangle
        indicesVec.push_back(bottom);
        indicesVec.push_back(top);
        indicesVec.push_back(nextTop);

        // Second triangle
        indicesVec.push_back(bottom);
        indicesVec.push_back(nextTop);
        indicesVec.push_back(nextBottom);
    }

    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES, indicesVec.size());
    for (unsigned int i = 0; i < indicesVec.size(); i++)
    {
        (*indices)[i] = indicesVec[i];
    }

    // Set up the geometry
    cylinderGeometry->setVertexArray(vertices.get());
    cylinderGeometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
    cylinderGeometry->setTexCoordArray(0, texcoords.get());
    cylinderGeometry->getPrimitiveSetList().push_back(indices.get());

    // Enable lighting
    cylinderGeometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);

    // Create material for red color
    osg::ref_ptr<osg::Material> material = new osg::Material();
    material->setAmbient(osg::Material::FRONT, osg::Vec4(0.8f, 0.0f, 0.0f, 1.0f));
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(1.0f, 0.0f, 0.0f, 1.0f));
    material->setSpecular(osg::Material::FRONT, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
    material->setShininess(osg::Material::FRONT, 12.0f);

    cylinderGeometry->getOrCreateStateSet()->setAttributeAndModes(material, osg::StateAttribute::ON);

    // Add geometry to geode
    cylinderGeode->addDrawable(cylinderGeometry);

    return cylinderGeode;
}

osg::ref_ptr<osg::Node> CreateYellowConeGeometry(double radius, double height, int segments)
{
    // Create geometry for a cone
    osg::ref_ptr<osg::Geode>    coneGeode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry> coneGeometry = new osg::Geometry();

    // Create vertices
    osg::ref_ptr<osg::Vec3Array> vertices  = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec3Array> normals   = new osg::Vec3Array();
    osg::ref_ptr<osg::Vec2Array> texcoords = new osg::Vec2Array();

    double h = height / 2.0;

    // Bottom center vertex
    vertices->push_back(osg::Vec3(0.0, 0.0, -h));
    normals->push_back(osg::Vec3(0.0, 0.0, -1.0));
    texcoords->push_back(osg::Vec2(0.5f, 0.5f));

    // Bottom circle vertices
    for (int i = 0; i <= segments; i++)
    {
        double angle = 2.0 * M_PI * i / segments;
        double x     = radius * cos(angle);
        double y     = radius * sin(angle);

        vertices->push_back(osg::Vec3(x, y, -h));
        normals->push_back(osg::Vec3(0.0, 0.0, -1.0));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 0.0f));
    }

    // Apex vertex
    vertices->push_back(osg::Vec3(0.0, 0.0, h));
    normals->push_back(osg::Vec3(0.0, 0.0, 1.0));
    texcoords->push_back(osg::Vec2(0.5f, 1.0f));

    // Side vertices
    for (int i = 0; i <= segments; i++)
    {
        double angle = 2.0 * M_PI * i / segments;
        double x     = radius * cos(angle);
        double y     = radius * sin(angle);

        // Calculate normal for cone side (perpendicular to surface)
        double nx     = cos(angle);
        double ny     = sin(angle);
        double nz     = radius / height;
        double length = sqrt(nx * nx + ny * ny + nz * nz);
        nx /= length;
        ny /= length;
        nz /= length;

        // Bottom side vertex
        vertices->push_back(osg::Vec3(x, y, -h));
        normals->push_back(osg::Vec3(nx, ny, nz));
        texcoords->push_back(osg::Vec2(static_cast<float>(i) / segments, 0.0f));
    }

    // Create indices for triangles
    std::vector<unsigned int> indicesVec;

    // Bottom cap
    for (int i = 1; i < segments; i++)
    {
        indicesVec.push_back(0);  // center
        indicesVec.push_back(i);
        indicesVec.push_back(i + 1);
    }

    // Sides
    int apexIndex      = segments + 2;
    int sideStartIndex = apexIndex + 1;
    for (int i = 0; i < segments; i++)
    {
        int bottom     = sideStartIndex + i;
        int nextBottom = sideStartIndex + i + 1;

        // First triangle
        indicesVec.push_back(apexIndex);
        indicesVec.push_back(bottom);
        indicesVec.push_back(nextBottom);
    }

    osg::ref_ptr<osg::DrawElementsUInt> indices = new osg::DrawElementsUInt(GL_TRIANGLES, indicesVec.size());
    for (unsigned int i = 0; i < indicesVec.size(); i++)
    {
        (*indices)[i] = indicesVec[i];
    }

    // Set up the geometry
    coneGeometry->setVertexArray(vertices.get());
    coneGeometry->setNormalArray(normals.get(), osg::Array::BIND_PER_VERTEX);
    coneGeometry->setTexCoordArray(0, texcoords.get());
    coneGeometry->getPrimitiveSetList().push_back(indices.get());

    // Enable lighting
    coneGeometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::ON);

    // Create material for yellow color
    osg::ref_ptr<osg::Material> material = new osg::Material();
    material->setAmbient(osg::Material::FRONT, osg::Vec4(0.8f, 0.8f, 0.0f, 1.0f));
    material->setDiffuse(osg::Material::FRONT, osg::Vec4(1.0f, 1.0f, 0.0f, 1.0f));
    material->setSpecular(osg::Material::FRONT, osg::Vec4(0.1f, 0.1f, 0.1f, 1.0f));
    material->setShininess(osg::Material::FRONT, 12.0f);

    coneGeometry->getOrCreateStateSet()->setAttributeAndModes(material, osg::StateAttribute::ON);

    // Add geometry to geode
    coneGeode->addDrawable(coneGeometry);

    return coneGeode;
}

osg::ref_ptr<osg::Geode> CreateTextLabelGeode(const PositionInfo& pos)
{
    // Create text content based on available information
    std::string text_content;

    if (pos.object_type.empty())
        text_content = pos.name;
    else
        text_content = pos.object_type + ": " + pos.name;

    if (text_content.empty())
        return nullptr;

    // Create geode for text
    osg::ref_ptr<osg::Geode> text_geode = new osg::Geode();

    // Create osgText::Text object
    osg::ref_ptr<osgText::Text> text = new osgText::Text();

    // Set text properties
    text->setText(text_content);
    // Use setCharacterSize to keep text size constant regardless of viewer zoom
    text->setCharacterSize(20.0f, 1.0f);  // size, aspectRatio
    text->setAlignment(osgText::Text::LEFT_CENTER);

    text->setCharacterSizeMode(osgText::Text::SCREEN_COORDS);
    if (pos.selected)
        text->setColor(osg::Vec4(0.0f, 1.0f, 0.0f, 1.0f));
    else
        text->setColor(osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // Set text position (centered at origin, we'll transform the parent)
    text->setPosition(osg::Vec3(0.0f, 0.0f, 0.0f));

    // Apply 180-degree rotation around Z-axis to flip text direction
    osg::Quat rotationQuat(M_PI, osg::Vec3(0.0f, 0.0f, 1.0f));
    text->setRotation(rotationQuat);

    // Add text to geode
    text_geode->addDrawable(text);

    return text_geode;
}

osg::ref_ptr<osg::Geode> CreateLineStripGeode(const std::vector<const PositionInfo*>& positions)
{
    // Create geometry for the trajectory line
    osg::ref_ptr<osg::Geode>    line_geode    = new osg::Geode();
    osg::ref_ptr<osg::Geometry> line_geometry = new osg::Geometry();

    // Create vertex array for the line
    osg::ref_ptr<osg::Vec3Array> line_vertices = new osg::Vec3Array();
    for (const auto* pos : positions)
    {
        line_vertices->push_back(osg::Vec3(static_cast<float>(pos->x),
                                           static_cast<float>(pos->y),
                                           static_cast<float>(pos->z + 30.0)));  // Slightly above ground
    }

    line_geometry->setVertexArray(line_vertices.get());

    // Create color array (cyan color for trajectory paths)
    osg::ref_ptr<osg::Vec4Array> line_colors = new osg::Vec4Array();
    line_colors->push_back(osg::Vec4(0.0f, 1.0f, 1.0f, 1.0f));  // Cyan
    line_geometry->setColorArray(line_colors.get());
    line_geometry->setColorBinding(osg::Geometry::BIND_OVERALL);

    // Create line strip primitive
    line_geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINE_STRIP, 0, line_vertices->size()));

    // Set line width
    osg::LineWidth* line_width = new osg::LineWidth();
    line_width->setWidth(3.0f);
    line_geometry->getOrCreateStateSet()->setAttributeAndModes(line_width, osg::StateAttribute::ON);

    // Disable lighting for the line
    line_geometry->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    line_geode->addDrawable(line_geometry.get());
    return line_geode;
}

// Function to create a simple box geometry (used when OSGB model loading fails)
osg::ref_ptr<osg::Node> CreateBoxGeometry(double width, double length, double height, const osg::Vec4& color)
{
    osg::ref_ptr<osg::Geode>    geode    = new osg::Geode;
    osg::ref_ptr<osg::Geometry> geometry = new osg::Geometry;

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back(osg::Vec3(-width / 2, -length / 2, -height / 2));
    vertices->push_back(osg::Vec3(width / 2, -length / 2, -height / 2));
    vertices->push_back(osg::Vec3(width / 2, length / 2, -height / 2));
    vertices->push_back(osg::Vec3(-width / 2, length / 2, -height / 2));
    vertices->push_back(osg::Vec3(-width / 2, -length / 2, height / 2));
    vertices->push_back(osg::Vec3(width / 2, -length / 2, height / 2));
    vertices->push_back(osg::Vec3(width / 2, length / 2, height / 2));
    vertices->push_back(osg::Vec3(-width / 2, length / 2, height / 2));

    geometry->setVertexArray(vertices);

    osg::ref_ptr<osg::DrawElementsUInt> faces = new osg::DrawElementsUInt(osg::PrimitiveSet::QUADS, 0);
    // Bottom face
    faces->push_back(0);
    faces->push_back(1);
    faces->push_back(2);
    faces->push_back(3);
    // Top face
    faces->push_back(4);
    faces->push_back(5);
    faces->push_back(6);
    faces->push_back(7);
    // Front face
    faces->push_back(0);
    faces->push_back(1);
    faces->push_back(5);
    faces->push_back(4);
    // Back face
    faces->push_back(2);
    faces->push_back(3);
    faces->push_back(7);
    faces->push_back(6);
    // Left face
    faces->push_back(3);
    faces->push_back(0);
    faces->push_back(4);
    faces->push_back(7);
    // Right face
    faces->push_back(1);
    faces->push_back(2);
    faces->push_back(6);
    faces->push_back(5);

    geometry->addPrimitiveSet(faces);

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back(color);
    geometry->setColorArray(colors, osg::Array::BIND_OVERALL);

    osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
    normals->push_back(osg::Vec3(0, 0, -1));  // Bottom
    normals->push_back(osg::Vec3(0, 0, 1));   // Top
    normals->push_back(osg::Vec3(0, -1, 0));  // Front
    normals->push_back(osg::Vec3(0, 1, 0));   // Back
    normals->push_back(osg::Vec3(-1, 0, 0));  // Left
    normals->push_back(osg::Vec3(1, 0, 0));   // Right

    geometry->setNormalArray(normals, osg::Array::BIND_PER_PRIMITIVE_SET);

    geode->addDrawable(geometry);
    return geode;
}
