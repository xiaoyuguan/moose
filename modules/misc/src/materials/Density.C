//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "Density.h"

registerMooseObject("MiscApp", Density);

InputParameters
Density::validParams()
{
  InputParameters params = Material::validParams();

  params.addCoupledVar("disp_r", "The r displacement");
  params.addCoupledVar("disp_x", "The x displacement");
  params.addCoupledVar("disp_y", "The y displacement");
  params.addCoupledVar("disp_z", "The z displacement");

  params.addCoupledVar(
      "displacements",
      "The displacements appropriate for the simulation geometry and coordinate system");

  params.addParam<std::string>("base_name",
                               "Optional parameter that allows the user to define "
                               "multiple material systems on the same block, "
                               "e.g. for multiple phases");
  params.addRequiredParam<Real>("density", "Density");
  params.addClassDescription("Creates density material property");

  return params;
}

Density::Density(const InputParameters & parameters)
  : Material(parameters),
    _is_coupled(true),
    _disp_r(isCoupled("displacements") ? coupledValue("displacements", 0)
                                       : (isCoupled("disp_r") ? coupledValue("disp_r") : _zero)),
    _base_name(isParamValid("base_name") ? getParam<std::string>("base_name") + "_" : ""),
    _orig_density(getParam<Real>("density")),
    _density(declareProperty<Real>(_base_name + "density"))
{
  // new parameter scheme
  if (isCoupled("displacements"))
  {
    // get coordinate system
    _coord_system = getBlockCoordSystem();

    // get coupled gradients
    const unsigned int ndisp = coupledComponents("displacements");
    _grad_disp.resize(ndisp);
    for (unsigned int i = 0; i < ndisp; ++i)
      _grad_disp[i] = &coupledGradient("displacements", i);

    // fill remaining components with zero
    _grad_disp.resize(3, &_grad_zero);
  }

  // old deprecated parameters
  else if (isCoupled("disp_x") || isCoupled("disp_r"))
  {
    // guess(!) coordinate system
    if (isCoupled("disp_r"))
    {
      if (isCoupled("disp_z"))
        _coord_system = Moose::COORD_RZ;
      else
        _coord_system = Moose::COORD_RSPHERICAL;
    }
    else
      _coord_system = Moose::COORD_XYZ;

    // couple gradients
    _grad_disp = {
        isCoupled("disp_x") ? &coupledGradient("disp_x")
                            : (isCoupled("disp_r") ? &coupledGradient("disp_r") : &_grad_zero),
        isCoupled("disp_y") ? &coupledGradient("disp_y")
                            : (isCoupled("disp_z") ? &coupledGradient("disp_z") : &_grad_zero),
        _coord_system != Moose::COORD_RZ && isCoupled("disp_z") ? &coupledGradient("disp_z")
                                                                : &_grad_zero};
  }

  // no coupling
  else
  {
    _is_coupled = false;
    // TODO: We should deprecate this case and have the user use a GenericConstantMaterial for this
  }
}

void
Density::initQpStatefulProperties()
{
  _density[_qp] = _orig_density;
}

void
Density::computeQpProperties()
{
  Real density = _orig_density;
  if (_is_coupled)
  {
    // rho * V = rho0 * V0
    // rho = rho0 * V0 / V
    // rho = rho0 / det(F)
    // rho = rho0 / det(grad(u) + 1)

    const Real Axx = (*_grad_disp[0])[_qp](0) + 1.0;
    const Real Axy = (*_grad_disp[0])[_qp](1);
    const Real Axz = (*_grad_disp[0])[_qp](2);
    const Real Ayx = (*_grad_disp[1])[_qp](0);
    Real Ayy = (*_grad_disp[1])[_qp](1) + 1.0;
    const Real Ayz = (*_grad_disp[1])[_qp](2);
    const Real Azx = (*_grad_disp[2])[_qp](0);
    const Real Azy = (*_grad_disp[2])[_qp](1);
    Real Azz = (*_grad_disp[2])[_qp](2) + 1.0;

    switch (_coord_system)
    {
      case Moose::COORD_XYZ:
        Azz = (*_grad_disp[2])[_qp](2) + 1.0;
        break;

      case Moose::COORD_RZ:
        if (_q_point[_qp](0) != 0.0)
          Azz = _disp_r[_qp] / _q_point[_qp](0) + 1.0;
        break;

      case Moose::COORD_RSPHERICAL:
        if (_q_point[_qp](0) != 0.0)
          Ayy = Azz = _disp_r[_qp] / _q_point[_qp](0) + 1.0;
        break;
    }

    const Real detF = Axx * Ayy * Azz + Axy * Ayz * Azx + Axz * Ayx * Azy - Azx * Ayy * Axz -
                      Azy * Ayz * Axx - Azz * Ayx * Axy;
    density /= detF;
  }

  _density[_qp] = density;
}
