//
// Author: Michal Nowicki
//

#include "types_six_dof_photo.h"
#include "../core/factory.h"
#include "../stuff/macros.h"

namespace g2o {
    using namespace std;

    bool EdgeInverseDepthPatch::write(std::ostream &os) const {
        os << _cam->id() << " ";
        for (int i = 0; i < 9; i++) {
            os << measurement()[i] << " ";
        }

        for (int i = 0; i < 9; i++)
            for (int j = i; j < 9; j++) {
                os << " " << information()(i, j);
            }
        return os.good();
    }

    bool EdgeInverseDepthPatch::read(std::istream &is) {
        int paramId;
        is >> paramId;
        setParameterId(0, paramId);

        for (int i = 0; i < 9; i++) {
            is >> _measurement[i];
        }
        for (int i = 0; i < 9; i++)
            for (int j = i; j < 9; j++) {
                is >> information()(i, j);
                if (i != j)
                    information()(j, i) = information()(i, j);
            }
        return true;
    }

    double EdgeInverseDepthPatch::getSubpixImageValue(double u, double v, std::vector< std::vector< float> > &image) {

        const double xInt = int(u), yInt = int(v);
        const double xSub = u - xInt, ySub = v - yInt;

        const double topLeft = (1.0 - xSub) * (1.0 - ySub);
        const double topRight = xSub * (1.0 - ySub);
        const double bottomLeft = (1.0 - xSub) * ySub;
        const double bottomRight = xSub * ySub;


        if (yInt < 0 || xInt < 0 || yInt + 1 >= image.size() || xInt + 1 >= image[0].size() )
        {
            return -1;
        }

        return topLeft * image[yInt][xInt] +
               topRight * image[yInt][xInt + 1] +
               bottomLeft * image[yInt + 1][xInt] +
               bottomRight * image[yInt + 1][xInt + 1];
    }

    void EdgeInverseDepthPatch::computeError() {

        const VertexSBAPointInvD *pointInvD = static_cast<const VertexSBAPointInvD *>(_vertices[0]);
        const VertexSE3ExpmapBright *T_p_from_world = static_cast<const VertexSE3ExpmapBright *>(_vertices[1]);
        const VertexSE3ExpmapBright *T_anchor_from_world = static_cast<const VertexSE3ExpmapBright *>(_vertices[2]);
        const CameraParameters *cam = static_cast<const CameraParameters *>(parameter(0));

        SE3QuatBright T_p_est = T_p_from_world->estimate();
        SE3QuatBright T_anchor_est = T_anchor_from_world->estimate();

        double cx = cam->principle_point[0], cy = cam->principle_point[1];
        double fx = cam->focal_length_x, fy = cam->focal_length_y;

        const float pyramidScale = imgAnchor[pyramidIndex]->imageScale;

        Matrix<double, 9, 1, Eigen::ColMajor> computedError;
        for (int i=0;i<neighbours.size();i++)
        {
            // Getting the patch value in anchor
            double refU = pointInvD->u0 / pyramidScale + neighbours[i].first ;
            double refV = pointInvD->v0 / pyramidScale + neighbours[i].second ;

            double refValue = getSubpixImageValue(refU, refV, imgAnchor[pyramidIndex]->image);

            // Getting the projected point in obs
            Eigen::Vector3d pointInFirst;
            pointInFirst[2] = 1. / pointInvD->estimate();
            pointInFirst[0] = (pointInvD->u0 - cx + neighbours[i].first * pyramidScale) * pointInFirst[2] / fx;
            pointInFirst[1] = (pointInvD->v0 - cy + neighbours[i].second * pyramidScale) * pointInFirst[2] / fy;

            // XYZ point in global
            Eigen::Vector3d pointInGlobal = T_anchor_est.se3quat.inverse().map(pointInFirst);

            // XYZ point in observation
            Eigen::Vector3d pointInObs = T_p_est.se3quat.map(pointInGlobal);

            // Projected point in observation
            Vector2d projectedPoint = cam->mostcam_map(pointInObs, baseline);

            // Find where the projected point is on selected pyramid lvl
            double obsU = projectedPoint[0] / pyramidScale;
            double obsV = projectedPoint[1] / pyramidScale;


            double obsValue = getSubpixImageValue(obsU, obsV, imgObs[pyramidIndex]->image);

            // Either of values is outside of the image
            if (refValue < 0 || obsValue < 0) {
                for (int j=0;j<9;j++)
                    computedError(j,0) = 255;
                break;
            }

//            computedError(i,0) = refValue  - obsValue;

            // Anchor left vs obs left
            if ( baseline < 0.0000001)
                computedError(i,0) = exp(T_p_est.aL) / exp(T_anchor_est.aL)  * (refValue - T_anchor_est.bL) - (obsValue - T_p_est.bL);
            // Anchor left vs anchor/obs right
            else
                computedError(i,0) = exp(T_p_est.aR) / exp(T_anchor_est.aL)  * (refValue - T_anchor_est.bL) - (obsValue - T_p_est.bR);

//            if (refValue - obsValue > 10) {
//                std::cout << "Projected and measured: (" << _measurement[0] << ", " << _measurement[1] << ") vs ("
//                          << projectedPoint[0] << ". " << projectedPoint[1] << ")" << std::endl;
//                std::cout << "computedError(i,0) = " << refValue << " " << obsValue << std::endl;
//            }
//            std:: cout << "refValue = " << refValue << " obsValue: " << obsValue << std::endl;

//             if (_vertices[1] == _vertices[2] && i == 0) {
//                 std::cout << " ----- " << std::endl << "The same pose: " << refV << " " << refU << " vs " << obsV << " " << obsU << std::endl;
//                 std::cout << "Values: " << refValue << " " << obsValue << std::endl;
//             }
        }

        _error = computedError;
    }

    inline Eigen::Matrix<double, 1, 2> EdgeInverseDepthPatch::d_inten_d_proj(const double obsU, const double obsV) {

        // The obs value will not be integer
        const double xInt = int(obsU), yInt = int(obsV);
        const double xSub = obsU - xInt, ySub = obsV - yInt;

        const double topLeft = (1.0 - xSub) * (1.0 - ySub);
        const double topRight = xSub * (1.0 - ySub);
        const double bottomLeft = (1.0 - xSub) * ySub;
        const double bottomRight = xSub * ySub;

        Eigen::Matrix<double, 1, 2> G;
        G.setZero();

        if (yInt < 0 || xInt < 0 || yInt + 1 >= imgObs[pyramidIndex]->gradient.size()
            || xInt + 1 >= imgObs[pyramidIndex]->gradient[0].size() )
        {
            return G;
        }

        for (int i=0;i<2;i++) {
            G(0,i) = topLeft * imgObs[pyramidIndex]->gradient[yInt][xInt][i] +
                     topRight * imgObs[pyramidIndex]->gradient[yInt + 1][xInt][i] +
                     bottomLeft * imgObs[pyramidIndex]->gradient[yInt][xInt + 1][i] +
                     bottomRight * imgObs[pyramidIndex]->gradient[yInt + 1][xInt + 1][i];
        }

//        std::cout << "Gradient test X: " << G(0,0) << " " << getSubpixImageValue(obsU-1, obsV, imgObs[pyramidIndex]->image)
//                  << " " << getSubpixImageValue(obsU+1, obsV, imgObs[pyramidIndex]->image) << std::endl;
//        std::cout << "Gradient test Y: " << G(0,1) << " " << getSubpixImageValue(obsU, obsV-1, imgObs[pyramidIndex]->image)
//                  << " " << getSubpixImageValue(obsU, obsV+1, imgObs[pyramidIndex]->image) << std::endl;
        return G;
    };

    inline Matrix<double, 2, 3, Eigen::ColMajor>
    EdgeInverseDepthPatch::d_proj_d_y(const double &fx, const double &fy, const Vector3D &xyz, const double &baseline) {
        double z_sq = xyz[2] * xyz[2];
        Matrix<double, 2, 3, Eigen::ColMajor> J;
        J << fx / xyz[2], 0, -(fx * (xyz[0]-baseline)) / z_sq,
                0, fy / xyz[2], -(fy * xyz[1]) / z_sq;
        return J;
    }

    inline Matrix<double, 3, 6, Eigen::ColMajor> EdgeInverseDepthPatch::d_expy_d_y(const Vector3D &y) {
        Matrix<double, 3, 6, Eigen::ColMajor> J;
        J.topLeftCorner<3, 3>() = -skew(y);
        J.bottomRightCorner<3, 3>().setIdentity();

        return J;
    }

    inline Matrix<double, 3, 1, Eigen::ColMajor>
    EdgeInverseDepthPatch::d_Tinvpsi_d_psi(const SE3Quat &T, const Vector3D &psi) {
        Matrix3D R = T.rotation().toRotationMatrix();
        Vector3D x = invert_depth(psi);
        Matrix<double, 3, 1, Eigen::ColMajor> J;
        J = -R * x;
        J *= 1. / psi.z();
        return J;
    }

    void EdgeInverseDepthPatch::linearizeOplus() {

        // Estimated values
        VertexSBAPointInvD *pointInvD = static_cast<VertexSBAPointInvD *>(_vertices[0]);
        VertexSE3ExpmapBright *vobsBright = static_cast<VertexSE3ExpmapBright *>(_vertices[1]);
        VertexSE3ExpmapBright *vanchorBright = static_cast<VertexSE3ExpmapBright *>(_vertices[2]);

        SE3QuatBright vobs = vobsBright->estimate();
        SE3QuatBright vanchor = vanchorBright->estimate();

        // Camera parameters
        const CameraParameters *cam
                = static_cast<const CameraParameters *>(parameter(0));
        double cx = cam->principle_point[0], cy = cam->principle_point[1];
        double fx = cam->focal_length_x, fy = cam->focal_length_y;


        // Empty jacobians - // TODO: Maybe only setZero is needed?
        _jacobianOplus[0] = Matrix<double, 9, 1, Eigen::ColMajor>();
        _jacobianOplus[1] = Matrix<double, 9, 10, Eigen::ColMajor>();
        _jacobianOplus[2] = Matrix<double, 9, 10, Eigen::ColMajor>();

        _jacobianOplus[0].setZero();
        _jacobianOplus[1].setZero();
        _jacobianOplus[2].setZero();

        // Getting current pose estimate
        SE3Quat T_cw = vobs.se3quat;

        // Getting the anchor pose estimate
        SE3Quat T_aw = vanchor.se3quat;

        // From anchor to current
        SE3Quat T_ca = T_cw * T_aw.inverse();

        const float pyramidScale = imgAnchor[pyramidIndex]->imageScale;

        // For all points in neighbourhood
        for (int i=0;i<9;i++) {

            // Getting the patch value in anchor
            double refU = pointInvD->u0 / pyramidScale + neighbours[i].first ;
            double refV = pointInvD->v0 / pyramidScale + neighbours[i].second ;

            double refValue = getSubpixImageValue(refU, refV, imgAnchor[pyramidIndex]->image);

            // Getting the projected point in obs
            Eigen::Vector3d pointInFirst;
            pointInFirst[2] = 1. / pointInvD->estimate();
            pointInFirst[0] = (pointInvD->u0 - cx + neighbours[i].first * pyramidScale) * pointInFirst[2] / fx;
            pointInFirst[1] = (pointInvD->v0 - cy + neighbours[i].second * pyramidScale) * pointInFirst[2] / fy;

            // Point in anchor in inverse depth parametrization
            Vector3D psi_a = invert_depth(pointInFirst);

            // Global point position
            Eigen::Vector3d pointInGlobal = T_aw.inverse().map(pointInFirst);

            // 3D point in obs
            Vector3D pointInObs = T_cw.map(pointInGlobal);

            // 2D projected point in observation
            Vector2d projectedPoint = cam->mostcam_map(pointInObs, baseline);

            // Find where the projected point is on selected pyramid lvl
            double obsU = projectedPoint[0] / pyramidScale;
            double obsV = projectedPoint[1] / pyramidScale;

            double obsValue = getSubpixImageValue(obsU, obsV, imgObs[pyramidIndex]->image);

            // Jacobian of camera
            Matrix<double, 2, 3, Eigen::ColMajor> Jcam
                    = d_proj_d_y(cam->focal_length_x/pyramidScale, cam->focal_length_y/pyramidScale, pointInObs, baseline);

            // Image gradient
            Matrix<double, 1, 2> Ji = d_inten_d_proj(obsU, obsV);

            // Jacobians of point, observation pose and anchor pose
            _jacobianOplus[0].row(i) = - Ji * Jcam * d_Tinvpsi_d_psi(T_ca, psi_a);


            // Different cases of the Jacobian of the local image
            double aObs = 0;
            double refValueMinusB = refValue - vanchor.bL;

            // 1) Stereo observation
            if (_vertices[1] == _vertices[2]) {
                aObs = vanchor.aR;

                _jacobianOplus[2](i,8) =  exp (aObs) / exp(vanchor.aL) * refValueMinusB;
                _jacobianOplus[2](i,9) =  1;

                _jacobianOplus[1] = _jacobianOplus[2];
            }
            else {
                _jacobianOplus[1].block<1,6>(i,0) = - Ji * Jcam * d_expy_d_y(pointInObs);

                // 2) left anchor vs left local
                if (baseline < 0.00001) {
                    aObs = vobs.aL;

                    _jacobianOplus[1](i, 6) = exp(aObs) / exp(vanchor.aL) * refValueMinusB;
                    _jacobianOplus[1](i, 7) = 1;


                }
                // 3) left anchor vs right local
                else {
                    aObs = vobs.aR;

                    _jacobianOplus[1](i, 8) = exp(aObs) / exp(vanchor.aL) * refValueMinusB;
                    _jacobianOplus[1](i, 9) = 1;
                }
            }

            // Jacobian w.r.t. the anchor pose left image
            _jacobianOplus[2].block<1,6>(i,0) = Ji * Jcam * T_ca.rotation().toRotationMatrix() * d_expy_d_y(pointInFirst);

            _jacobianOplus[2](i,6) = - exp(aObs) / exp(vanchor.aL) * refValueMinusB;
            _jacobianOplus[2](i,7) = - exp(aObs) / exp(vanchor.aL);
        }
    }

    bool EdgeInverseDepthPatch::isDepthPositive() {

        const VertexSBAPointInvD *pointInvD = static_cast<const VertexSBAPointInvD *>(_vertices[0]);
        const VertexSE3ExpmapBright *T_p_from_world = static_cast<const VertexSE3ExpmapBright *>(_vertices[1]);
        const VertexSE3ExpmapBright *T_anchor_from_world = static_cast<const VertexSE3ExpmapBright *>(_vertices[2]);
        const CameraParameters *cam = static_cast<const CameraParameters *>(parameter(0));

        SE3QuatBright T_p_est = T_p_from_world->estimate();
        SE3QuatBright T_anchor_est = T_anchor_from_world->estimate();

        double cx = cam->principle_point[0], cy = cam->principle_point[1];
        double fx = cam->focal_length_x, fy = cam->focal_length_y;

        if (pointInvD->estimate() < 0.000000000001)
            return false;

        Eigen::Vector3d pointInAnchor;
        pointInAnchor[2] = 1. / pointInvD->estimate();
        pointInAnchor[0] = (pointInvD->u0 - cx) * pointInAnchor[2] / fx;
        pointInAnchor[1] = (pointInvD->v0 - cy) * pointInAnchor[2] / fy;

        Eigen::Vector3d pointInGlobal = T_anchor_est.se3quat.inverse().map(pointInAnchor);
        Eigen::Vector3d pointInObs = T_p_est.se3quat.map(pointInGlobal);

        return pointInAnchor(2) > 0.0 && pointInObs(2) > 0.0;
    }


}