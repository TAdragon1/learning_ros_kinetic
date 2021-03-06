// rrbot_fk_ik library implementation file; start w/ fwd kin

#include <rrbot/rrbot_kinematics.h>
using namespace std;

Eigen::Matrix4d compute_A_of_DH(double q, double a, double d, double alpha) {
    Eigen::Matrix4d A;
    Eigen::Matrix3d R;
    Eigen::Vector3d p;

    A = Eigen::Matrix4d::Identity();
    R = Eigen::Matrix3d::Identity();
    //ROS_INFO("compute_A_of_DH: a,d,alpha,q = %f, %f %f %f",a,d,alpha,q);
    //could correct q w/ q_offset now...
    double cq = cos(q);
    double sq = sin(q);
    double sa = sin(alpha);
    double ca = cos(alpha);
    R(0, 0) = cq;
    R(0, 1) = -sq*ca; //% - sin(q(i))*cos(alpha);
    R(0, 2) = sq*sa; //%sin(q(i))*sin(alpha);
    R(1, 0) = sq;
    R(1, 1) = cq*ca; //%cos(q(i))*cos(alpha);
    R(1, 2) = -cq*sa; //%	
    //%R(3,1)= 0; %already done by default
    R(2, 1) = sa;
    R(2, 2) = ca;
    p(0) = a * cq;
    p(1) = a * sq;
    p(2) = d;
    A.block<3, 3>(0, 0) = R;
    A.col(3).head(3) = p;
    return A;
}

Rrbot_fwd_solver::Rrbot_fwd_solver() {
    //construct the tool transform from defined constants
    Eigen::Matrix3d R_base_link_wrt_world;
    Eigen::Vector3d O_base_link_wrt_world;

    A_base_link_wrt_world_ = Eigen::MatrixXd::Identity(4, 4);
    O_base_link_wrt_world(0) = base_to_frame0_dx;
    O_base_link_wrt_world(1) = base_to_frame0_dy;
    O_base_link_wrt_world(2) = base_to_frame0_dz;

    // choose frame0 x-axis parallel to world-frame z-axis, 
    R_base_link_wrt_world(0, 0) = 0;
    R_base_link_wrt_world(1, 0) = 0; //% - sin(q(i))*cos(alpha);
    R_base_link_wrt_world(2, 0) = 1; //
    // frame0 y-axis is anti-parallel to world-frame x-axis;
    R_base_link_wrt_world(0, 1) = -1;
    R_base_link_wrt_world(1, 1) = 0; //
    R_base_link_wrt_world(2, 1) = 0; //%
    //DH frame0 has z-axis through joint1, antiparallel to world-y axis
    R_base_link_wrt_world(0, 2) = 0.0;
    R_base_link_wrt_world(1, 2) = -1.0;
    R_base_link_wrt_world(2, 2) = 0.0;

    A_base_link_wrt_world_.block<3, 1>(0, 3) = O_base_link_wrt_world;
    A_base_link_wrt_world_.block<3, 3>(0, 0) = R_base_link_wrt_world;
    A_base_link_wrt_world_inv_ = A_base_link_wrt_world_.inverse();

}

Eigen::Affine3d Rrbot_fwd_solver::fwd_kin_flange_wrt_world_solve(Eigen::VectorXd q_vec) {
    Eigen::Matrix4d M;
    M = fwd_kin_solve_(q_vec);
    Eigen::Affine3d A(M); //convert 4x4 matrix to an Affine object
    return A;
}

Eigen::Matrix4d Rrbot_fwd_solver::fwd_kin_solve_(Eigen::VectorXd q_vec) {
    Eigen::Matrix4d A = Eigen::Matrix4d::Identity();
    //%compute A matrix of frame i wrt frame i-1 for each joint:
    Eigen::Matrix4d A_i_iminusi;
    Eigen::Matrix3d R;
    Eigen::Vector3d p;
    //cout << "A_base_link_wrt_world_:" << endl;
    //cout << A_base_link_wrt_world_ << endl;
    for (int i = 0; i < NJNTS; i++) {
        A_i_iminusi = compute_A_of_DH(q_vec[i] + DH_q_offsets[i], DH_a_params[i], DH_d_params[i], DH_alpha_params[i]);
        A_mats_[i] = A_i_iminusi;
        //cout << "A" << i << ": " << endl;
        //cout << A_i_iminusi << endl;
    }
    //now, multiply these together
    //A_base_link_wrt_world_ * A_frame1_wrt_base_link = A_frame1_wrt_world
    A_mat_products_[0] = A_base_link_wrt_world_ * A_mats_[0];

    for (int i = 1; i < NJNTS; i++) {
        A_mat_products_[i] = A_mat_products_[i - 1] * A_mats_[i];
    }
    //Eigen::Vector4d test_0_vec;
    //test_0_vec<<0,0,0,1;
    //cout<<"test Amat prod: "<<A_base_link_wrt_world_*A_mats_[0]*A_mats_[1] *test_0_vec<<endl;
    return A_mat_products_[NJNTS - 1]; //tool flange frame
}



//Jacobian, 6x2
//angular part is just b axis of each frame;
//translational part depends on cross products, including vec from frame origin to endpoint
// and joint-axis vector

Eigen::MatrixXd Rrbot_fwd_solver::Jacobian(Eigen::VectorXd q_vec) {
    Eigen::MatrixXd Jacobian(6, NJNTS); // = Eigen::Zeros(6,7);  
    //Eigen::Affine3d affine_flange; 
    Eigen::MatrixXd Origins(3, NJNTS);
    Eigen::Matrix4d A4x4_flange;
    Eigen::MatrixXd J_ang(3, NJNTS), J_trans(3, NJNTS);
    Eigen::Vector3d zvec, Oi, wvec, rvec;

    //affine_flange = fwd_kin_flange_wrt_base_solve(q_vec); //compute all the A matrices and their products
    A4x4_flange = fwd_kin_solve_(q_vec);
    wvec = A4x4_flange.block<3, 1>(0, 3); // get vector from base to endpoint


    //compute the angular Jacobian, using z-vecs from each frame; first frame is just [0;0;1]
    zvec = A_base_link_wrt_world_.block<3, 1>(0, 2);
    //cout<<"setting first ang vec in J_ang: "<<endl;
    J_ang.block<3, 1>(0, 0) = zvec; // and populate J_ang with them; at present, this is not being returned
    //cout<<"setting first origin: "<<endl;
    Oi << A_base_link_wrt_world_.block<3, 1>(0, 3);
    Origins.block<3, 1>(0, 0) = Oi;
    for (int i = 1; i < NJNTS; i++) {
        zvec = A_mat_products_[i - 1].block<3, 1>(0, 2); //%strip off z axis of each previous frame; note subscript slip  
        J_ang.block<3, 1>(0, i) = zvec; // and populate J_ang with them;
        Oi = A_mat_products_[i - 1].block<3, 1>(0, 3); //origin of i'th frame
        Origins.block<3, 1>(0, i) = Oi;
    }
    //now, use the zvecs to help compute J_trans
    for (int i = 0; i < NJNTS; i++) {
        zvec = J_ang.block<3, 1>(0, i); //%recall z-vec of current axis     
        Oi = Origins.col(i); //block<3, 1>(0, i); //origin of i'th frame
        rvec = wvec - Oi; //%vector from origin of i'th frame to wrist pt 
        //Rvecs.block<3, 1>(0, i) = rvec; //save these?
        //t1 = zvecs.block<3, 1>(0, i);
        //t2 = rvecs.block<3, 1>(0, i);
        J_trans.block<3, 1>(0, i) = zvec.cross(rvec);
        //cout<<"frame "<<i<<": zvec = "<<zvec.transpose()<<"; Oi = "<<Oi.transpose()<<endl;
    }
    //assemble into combined Jacobian:
    Jacobian.block<3, NJNTS>(0, 0) = J_trans;
    Jacobian.block<3, NJNTS>(3, 0) = J_ang;
    return Jacobian;
}





//-------------------------IK methods---------------------------------
//constructor:

Rrbot_IK_solver::Rrbot_IK_solver() {
    //constructor: 
    ROS_INFO("Rrbot_IK_solver constructor");
}

//given desired origin of tool flange, find any/all combinations of (q1,q2) that achieve this goal
// return these via reference (vector) variable  q_solns
// return int = number of valid solns
// solution is valid only if it achieves desired point within tolerance AND uses reachable joint angles

int Rrbot_IK_solver::ik_solve(Eigen::Affine3d desired_flange_pose_wrt_base, std::vector<Eigen::Vector2d> &q_solns) {
    Eigen::Vector3d O_flange_wrt_world = desired_flange_pose_wrt_base.translation();
    q_solns.clear();
    double q_elbow, q_shoulder;
    Eigen::Vector2d q_soln_vec;
    int num_solns = 0;
    bool valid_q_elbow = false;
    bool valid_q_shoulder = false;
    std::vector<double> q_elbow_solns;
    // find the elbow solns; only considers solns within joint range:
    valid_q_elbow = solve_for_elbow_ang(O_flange_wrt_world, q_elbow_solns);
    if (!valid_q_elbow) return 0; // zero valid solns

    // for each elbow soln, find shoulder soln:
    q_elbow = q_elbow_solns[0];
    valid_q_shoulder = solve_for_shoulder_ang(O_flange_wrt_world, q_elbow, q_shoulder);
    if (valid_q_shoulder) {
        q_soln_vec[0] = q_shoulder; 
        q_soln_vec[1] = q_elbow;
        q_solns.push_back(q_soln_vec);
        num_solns = 1;
    }

    if (q_elbow_solns.size() > 1) {
        q_elbow = q_elbow_solns[1];
        valid_q_shoulder = solve_for_shoulder_ang(O_flange_wrt_world, q_elbow, q_shoulder);
        if (valid_q_shoulder) {
        q_soln_vec[0] = q_shoulder; 
        q_soln_vec[1] = q_elbow;
            q_solns.push_back(q_soln_vec);
            num_solns++;
        }
    }
    return num_solns;
}

bool Rrbot_IK_solver::solve_for_elbow_ang(Eigen::Vector3d O_flange_wrt_world, std::vector<double> &q_elbow_solns) {
    Eigen::Vector3d O_shoulder_wrt_world;
    O_shoulder_wrt_world = A_base_link_wrt_world_.block<3, 1>(0, 3);

    Eigen::Vector3d vec_shoulder_to_flange;
    vec_shoulder_to_flange = O_flange_wrt_world - O_shoulder_wrt_world;

    //compute distance from shoulder to flange ONLY in x-z plane
    vec_shoulder_to_flange(1) = 0.0; //suppress delta-y

    double d_shoulder_to_flange = vec_shoulder_to_flange.norm(); //dist from shoulder to flange

    //law of cosines: C^2 = A^2 + B^2 - 2ABcos(C_ang)
    double den = 2.0 * DH_a1*DH_a2;
    double num = d_shoulder_to_flange * d_shoulder_to_flange - (DH_a1)*(DH_a1) - DH_a2 * DH_a2;
    //test viability here...
    double c_ang = num / den;
    if (c_ang > 1.0) {
        ROS_WARN("flange out of reach at full elbow extension");
        return false;
    }
    //cout<<"num, den, c4 = "<<num<<", "<<den<<", "<<c4<<endl;
    double s_ang = sqrt(1 - c_ang * c_ang);

    double q_elbow_a = atan2(s_ang, c_ang);
    //cout<<"q4a = "<<q4a<<endl;
    q_elbow_solns.clear();
    // test limits DH_q_max4>= q4 >= DH_q_min4
    bool valid_soln = false;
    if (fit_q_to_range(q_lower_limits[1], q_upper_limits[1], q_elbow_a)) {
        valid_soln = true;
        q_elbow_solns.push_back(q_elbow_a);
        //cout<<"q4a = "<<q4a<<endl;
    }
    double q_elbow_b = atan2(-s_ang, c_ang);
    if (fit_q_to_range(q_lower_limits[1], q_upper_limits[1], q_elbow_b)) {
        valid_soln = true;
        q_elbow_solns.push_back(q_elbow_b);
        //cout<<"q4b = "<<q4b<<endl;
    }
    return valid_soln;
}

//given desired flange origin and elbow angle, solve for shoulder angle
//[O_flange_wrt_world; 1 ] = A_base_link_wrt_world_*A_mats_[0]*A_mats[1] *[0;0;0;1];
// so, A_base_link_wrt_world_inv*[O_flange_wrt_world; 1 ] = A_mats[0]*(A_mats[1]*[0;0;0;1])
// is of form: y_vec = A(q)*b_vec
// look at first row:
//  y_vec[0] = [cos(q0),  -sin(q0),  0  a1*cos(q0)]*[b_vec]
// which is of the form: K = A*cos(q) + B*sin(q), which can be solved by transforming to cylindrical coords, as follows

bool Rrbot_IK_solver::solve_for_shoulder_ang(Eigen::Vector3d O_flange_wrt_world, double q_elbow, double &q_shoulder) {
    Eigen::Vector4d b_vec, y_vec, O_flange_4x1, O_flange_test;
    Eigen::Matrix4d A2_wrt_1, A1_wrt_0;
    double fk_tol = 0.00001; //choose tolerance to eval if soln is a fit
    A2_wrt_1 = compute_A_of_DH(q_elbow, DH_a2, DH_d2, DH_alpha2);
    //cout << "A2_wrt_1= " << endl;
    //cout << A2_wrt_1 << endl;
    b_vec = A2_wrt_1.block<4, 1>(0, 3); // extract fourth column
    //cout << "bvec = " << b_vec.transpose() << endl;
    O_flange_4x1.block<3, 1>(0, 0) = O_flange_wrt_world; //convert target position to homogeneous coords
    O_flange_4x1(3) = 1.0;
    //cout << "O_flange_4x1 = " << O_flange_4x1.transpose() << endl;
    y_vec = A_base_link_wrt_world_inv_*O_flange_4x1;
    //cout << "y_vec: " << y_vec.transpose() << endl;
    double K = y_vec(0);
    double A = b_vec(0) + DH_a1;
    double B = -b_vec(1);
    //cout << "K=" << K << ", A=" << A << ", B=" << B << endl;
    bool valid_solns = false;
    std::vector<double> q_solns;
    valid_solns = solve_K_eq_Acos_plus_Bsin(K, A, B, q_solns);
    if (!valid_solns) return false; // no valid shoulder angles

    //cout << "shoulder solns: " << q_solns[0] << ", " << q_solns[1] << endl;
    double fk_err;
    //test the soln(s) to see if have a valid soln:
    q_shoulder = q_solns[0];
    //is this soln within joint range?
    if (fit_q_to_range(q_lower_limits[0], q_upper_limits[0], q_shoulder)) {
        A1_wrt_0 = compute_A_of_DH(q_shoulder, DH_a1, DH_d1, DH_alpha1);
        O_flange_test = A_base_link_wrt_world_ * A1_wrt_0*b_vec;
        fk_err = (O_flange_test - O_flange_4x1).norm();
        //cout << "shoulder soln 1: fk err = " << fk_err << endl;
        if (fk_err < 0.00001) {
            //cout << "accepting this soln" << endl;
            return true; // q_shoulder is valid;
        }
    }
    //if here, soln 0 was not viable; try 2nd soln, if it exists
    if (q_solns.size() > 1) {
        q_shoulder = q_solns[1];
        //is this soln within joint range?
        if (fit_q_to_range(q_lower_limits[0], q_upper_limits[0], q_shoulder)) {
            A1_wrt_0 = compute_A_of_DH(q_shoulder, DH_a1, DH_d1, DH_alpha1);
            O_flange_test = A_base_link_wrt_world_ * A1_wrt_0*b_vec;
            fk_err = (O_flange_test - O_flange_4x1).norm();
            //cout << "shoulder soln 2: fk err = " << fk_err << endl;
            if (fk_err < 0.00001) {
                //cout << "accepting this soln" << endl;
                return true; // q_shoulder is valid;
            }
        }
    }

    return false; //if here, no valid solns
}

//given qmin, qmax and q, coerce q into a periodic soln between q_min and q_max, if possible
//return "false" if not possible

bool Rrbot_IK_solver::fit_q_to_range(double q_min, double q_max, double &q) {
    //cout<<"fit_q_to_range: q_min = "<<q_min<<", q_max = "<<q_max<<", q_in = "<<q<<endl;
    while (q < q_min) {
        q += 2.0 * M_PI;
    }
    while (q > q_max) {
        q -= 2.0 * M_PI;
    }
    //cout<<"q_fit = "<<q<<endl;
    if (q < q_min)
        return false;
    else
        return true;
}


//solve the eqn r = A*cos(q) + B*sin(q) for q; return "true" if at least one soln is valid
bool Rrbot_IK_solver::solve_K_eq_Acos_plus_Bsin(double K, double A, double B, std::vector<double> &q_solns) {
    double r, cphi, sphi, phi, gamma;
    double KTOL = 0.000001; //arbitrary tolerance
    r = sqrt(A * A + B * B);
    phi = atan2(B, A);
    q_solns.clear();
    if (fabs(K) > fabs(r)) {
        ROS_WARN("K/r is too large for a cos/sin soln");
        return false; //if |K/r|>1, no solns
    }
    //could still have trouble w/ K=0...
    if (fabs(K) < KTOL) {
        ROS_WARN("K is too small for A,B,K trig soln: user error? ");
        return false; //illegal use of this fnc
    }
    gamma = acos(K / r);
    double soln1 = phi + gamma;
    double soln2 = phi - gamma;
    q_solns.push_back(soln1);
    q_solns.push_back(soln2);
    //test/DEBUG
    double q_soln;

    /* debug...
    //cout << "K = " << K << endl;
    //cout << "soln1=" << soln1 << endl;
    double test = A * cos(soln1) + B * sin(soln1);
    //cout << "Acos(q1) + Bsin(q1) = " << test << endl;
    if (fabs(test - K) < KTOL) {
        q_soln = soln1;
        //cout << "soln1 w/in tol" << endl;
    }

    //cout << "soln2=" << soln2 << endl;
    //cout << "Acos(q2) + Bsin(q2) = " << test << endl;
    test = A * cos(soln2) + B * sin(soln2);
    if (fabs(test - K) < KTOL) {
        q_soln = soln2;
        //cout << "soln2 w/in tol" << endl;
    }
   */

    return true;
}
