// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2016 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "openMVG/image/image_io.hpp"
#include "openMVG/spherical/cubic_image_sampler.hpp"

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "third_party/vectorGraphics/svgDrawer.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace svg;

// Convert spherical panorama to rectilinear images
int main(int argc, char **argv)
{
  CmdLine cmd;

  std::string
    s_directory_in,
    s_directory_out;
  int
    image_resolution = 1024,
    nb_split = 5;
  float fov = 60.0;

  // required
  cmd.add( make_option('i', s_directory_in, "input_dir") );
  cmd.add( make_option('o', s_directory_out, "output_dir") );
  // Optional
  cmd.add( make_option('r', image_resolution, "image_resolution") );
  cmd.add( make_option('n', nb_split, "nb_split") );
  cmd.add( make_option('f', fov, "fov") );
  cmd.add( make_switch('D', "demo_mode") );


  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch (const std::string& s) {
    std::cerr << "Usage: " << argv[0] << '\n'
    << "[-i|--input_dir] the path where the spherical panoramic images are saved \n"
    << "[-o|--output_dir] the path where output rectilinear image will be saved \n"
    << " OPTIONAL:\n"
    << "[-r|--image_resolution] the rectilinear image size (default:" << image_resolution << ") \n"
    << "[-n|--nb_split] the number of rectilinear image along the X axis (default:" << nb_split << ") \n"
    << "[-f|--fov] the rectilinear camera FoV in degrees (default:" << fov << ") \n"
    << "[-D|--demo_mode] switch parameter, export a SVG file that simulate asked rectilinear\n"
    << "  frustum configuration on the spherical image.\n"
    << std::endl;
  }

  // Input parameter checking

  if (image_resolution < 0)
  {
    std::cerr << "image_resolution must be larger than 0" << std::endl;
    return EXIT_FAILURE;
  }
  if (nb_split < 0)
  {
    std::cerr << "nb_split must be larger than 0" << std::endl;
    return EXIT_FAILURE;
  }
  if (s_directory_in.empty() || s_directory_out.empty())
  {
    std::cerr << "input_dir and output_dir option must not be empty" << std::endl;
    return EXIT_FAILURE;
  }

  if (!stlplus::is_folder(s_directory_out))
  {
    if (!stlplus::folder_create(s_directory_out))
    {
      std::cerr << "Cannot create the output_dir directory" << std::endl;
      return EXIT_FAILURE;
    }
  }

  // List images from the input directory
  const std::vector<std::string> vec_filenames
    = stlplus::folder_wildcard(s_directory_in, "*.jpg", false, true);

  if (vec_filenames.empty())
  {
    std::cerr << "Did not find any jpg image in the provided input_dir" << std::endl;
    return EXIT_FAILURE;
  }

  using namespace openMVG;

  //-----------------
  //-- Create N rectilinear cameras
  //     (according the number of asked split along the X axis)
  //-- For each camera
  //   -- Forward mapping
  //   -- Save resulting images to disk
  //-----------------


  //-- Simulate a camera with many rotations along the Y axis
  const int pinhole_width = image_resolution, pinhole_height = image_resolution;
  const double focal = spherical::FocalFromPinholeHeight(pinhole_height, openMVG::D2R(fov));
  const openMVG::cameras::Pinhole_Intrinsic pinhole_camera = spherical::ComputeCubicCameraIntrinsics(image_resolution);

  const double alpha = (M_PI * 2.0) / static_cast<double>(nb_split); // 360 / split_count
  std::vector<Mat3> camera_rotations;
  for (int i = 0; i < nb_split; ++i)
  {
    camera_rotations.emplace_back(RotationAroundY(alpha * i));
  }

  if (cmd.used('D')) // Demo mode:
  {
    // Create a spherical camera:
    const int pano_width = 4096, pano_height = pano_width / 2;
    const openMVG::cameras::Intrinsic_Spherical sphere_camera(pano_width, pano_height);

    svgDrawer svgStream(pano_width, pano_height);
    svgStream.drawLine(0, 0, pano_width, pano_height, svgStyle());
    svgStream.drawLine(pano_width, 0, 0, pano_height, svgStyle());

    //--> For each cam, reproject the image borders onto the panoramic image

    for (const Mat3 & cam_rotation : camera_rotations)
    {
      // Draw the shot border with the givenStep:
      const int step = 10;
      // Store the location of the pinhole bearing vector projection in the spherical image
      Vec2 sphere_proj;

      // Vertical rectilinear image border:
      for (double j = 0; j <= image_resolution; j += image_resolution/(double)step)
      {
        // Project the pinhole bearing vector to the sphere
        sphere_proj = sphere_camera.project(cam_rotation * pinhole_camera(Vec2(0., j)));
        svgStream.drawCircle(sphere_proj.x(), sphere_proj.y(), 4, svgStyle().fill("green"));

        sphere_proj = sphere_camera.project(cam_rotation * pinhole_camera(Vec2(image_resolution, j)));
        svgStream.drawCircle(sphere_proj.x(), sphere_proj.y(), 4, svgStyle().fill("green"));
      }
      // Horizontal rectilinear image border:
      for (double j = 0; j <= image_resolution; j += image_resolution/(double)step)
      {
        sphere_proj = sphere_camera.project(cam_rotation * pinhole_camera(Vec2(j, 0.)));
        svgStream.drawCircle(sphere_proj.x(), sphere_proj.y(), 4, svgStyle().fill("yellow"));

        sphere_proj = sphere_camera.project(cam_rotation * pinhole_camera(Vec2(j, image_resolution)));
        svgStream.drawCircle(sphere_proj.x(), sphere_proj.y(), 4, svgStyle().fill("yellow"));
      }
    }

    std::ofstream svgFile( stlplus::create_filespec(s_directory_out, "test.svg" ));
    svgFile << svgStream.closeSvgFile().str();

    return EXIT_SUCCESS;
  }

  //-- For each input image extract multiple pinhole images
  for (const std::string & filename_it : vec_filenames)
  {
    image::Image<image::RGBColor> spherical_image;
    if (!ReadImage(stlplus::create_filespec(s_directory_in, filename_it).c_str(), &spherical_image))
    {
      std::cerr << "Cannot read the image: " << stlplus::create_filespec(s_directory_in, filename_it) << std::endl;
      continue;
    }

    std::vector<image::Image<image::RGBColor>> sampled_images(
        camera_rotations.size(), image::Image<image::RGBColor>(image_resolution, image_resolution, image::BLACK));

    spherical::SphericalToPinholes
    (
      spherical_image,
      pinhole_camera,
      sampled_images,
      camera_rotations,
      image::Sampler2d<image::SamplerLinear>()
    );

    // Save images to disk
    for (int i_rot = 0; i_rot < camera_rotations.size(); ++i_rot)
    {
      //-- save image
      const std::string basename = stlplus::basename_part(filename_it);

      std::cout << basename << " cam index: " << i_rot << std::endl;

      std::ostringstream os;
      os << s_directory_out << "/" << basename << "_" << i_rot << ".jpg";
      WriteImage(os.str().c_str(), sampled_images[i_rot]);
    }
  }

  std::ofstream fileFocalOut(stlplus::create_filespec(s_directory_out, "focal.txt"));
  fileFocalOut << focal;
  fileFocalOut.close();

  return EXIT_SUCCESS;
}
