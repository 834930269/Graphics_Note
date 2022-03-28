﻿#include <vector>
#include <limits>
#include <iostream>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"
#include "our_gl.h"


Model* model = NULL;
float* shadowbuffer = NULL;

const int width = 800;
const int height = 800;

Vec3f light_dir(1, 1, 0);
Vec3f eye(1, 1, 4);
Vec3f center(0, 0, 0);
Vec3f up(0, 1, 0);

struct Shader : public IShader {
	//下面有一些需要双pass的变量是通过上一个pass传入的
	//进入两个着色器时,已经设置好了(准确来说,是在初始化就设置好了)
	mat<4, 4, float> uniform_M;		//Projection*ModelView
	mat<4, 4, float> uniform_MIT;	//(Projection*ModelView).invert_transpose();
	mat<4, 4, float> uniform_Mshadow;	//shadowbuffer转换到屏幕空间
	mat<2, 3, float> varying_uv;		//三角形uv坐标,顶点写入,片元读取
	mat<3, 3, float> varying_tri;		//三角形坐标

	Shader(Matrix M, Matrix MIT, Matrix MS) : uniform_M(M), uniform_MIT(MIT), uniform_Mshadow(MS), varying_uv(), varying_tri() {}

	//模型的第i个三角形,第n个顶点
	virtual Vec4f vertex(int iface, int nthvert) {
		//设置设置三个点的uv
		varying_uv.set_col(nthvert, model->uv(iface, nthvert));
		Vec4f gl_Vertex = Viewport * Projection * ModelView * embed<4>(model->vert(iface, nthvert));
		varying_tri.set_col(nthvert, proj<3>(gl_Vertex / gl_Vertex[3]));
		return gl_Vertex;
	}

	//bar是重心坐标,color是颜色
	virtual bool fragment(Vec3f bar, TGAColor& color) {
		//shadowmap_projetcion
		Vec4f sb_p = uniform_Mshadow * embed<4>(varying_tri * bar);
		sb_p = sb_p / sb_p[3];
		int idx = int(sb_p[0]) + int(sb_p[1]) * width;	//shadowbubffer数组的index,x,y坐标到一维index
		//shadow表示被遮挡的程度吧,硬阴影
		float shadow = .3 + .7 * (shadowbuffer[idx] < sb_p[2]+43.44);//magic coeff
		Vec2f uv = varying_uv * bar;	//计算当前pixel的uv坐标 
		Vec3f n = proj<3>(uniform_MIT * embed<4>(model->normal(uv))).normalize();	//法线
		Vec3f l = proj<3>(uniform_M * embed<4>(light_dir)).normalize();	//光向量
		Vec3f r = (n * (n * l * 2.f) - l).normalize();   // 光反射方向
		float spec = pow(std::max(r.z, 0.0f), model->specular(uv));	//高光
		float diff = std::max(0.f, n * l);
		TGAColor c = model->diffuse(uv);
		for (int i = 0; i < 3; ++i) {
			color[i] = std::min<float>(20 + c[i] * shadow * (1.2 * diff + .6 * spec), 255);
		}
		return false;	//不剪切
	}
};

struct DepthShader : public IShader {
	mat<3, 3, float> varying_tri;
	DepthShader() :varying_tri() {}
	virtual Vec4f vertex(int iface, int nthvert) {
		Vec4f gl_Vertex = embed<4>(model->vert(iface, nthvert));	//从.obj中读取顶点数据
		gl_Vertex = Viewport * Projection * ModelView * gl_Vertex;	//转换到屏幕空间
		//齐次坐标除w
		varying_tri.set_col(nthvert, proj<3>(gl_Vertex / gl_Vertex[3]));
		return gl_Vertex;
	}
	virtual bool fragment(Vec3f bar, TGAColor& color) {
		Vec3f p = varying_tri * bar;
		//将z坐标和最大深度(2000)除一下归一化(深度图)
		//这里前面有zBuffer限制着,所以这里直接赋值就可以了
		color = TGAColor(255, 255, 255) * (p.z / depth);
		return false;
	}
};


int main(int argc,char **argv) {
	if (2 == argc) {
		model = new Model(argv[1]);
	}
	else {
		model = new Model("obj/african_head.obj");
	}
	
	float* zbuffer = new float[width * height];
	shadowbuffer = new float[width * height];
	for (int i = width * height; --i;) {
		zbuffer[i] = shadowbuffer[i] = -std::numeric_limits<float>::max();
	}

	light_dir.normalize();

	{//渲染shadowbuffer
		TGAImage depth(width, height, TGAImage::RGB);
		lookat(light_dir, center, up);
		viewport(width / 8, height / 8, width * 3 / 4, height * 3 / 4);
		projection(0);

		DepthShader depthshader;
		Vec4f screen_coords[3];
		for (int i = 0; i < model->nfaces(); i++) {
			for (int j = 0; j < 3; j++) {
				screen_coords[j] = depthshader.vertex(i, j);
			}
			triangle(screen_coords, depthshader, depth, shadowbuffer);
		}
		depth.flip_vertically();
		depth.write_tga_file("depth.tga");
	}
	
	Matrix M = Viewport * Projection * ModelView;

	{//渲染帧buffer
		TGAImage frame(width, height, TGAImage::RGB);
		lookat(eye, center, up);
		viewport(width / 8, height / 8, width * 3 / 4, height * 3 / 4);
		projection(-1.f / (eye - center).norm());

		Shader shader(ModelView, (Projection * ModelView).invert_transpose(), M * (Viewport * Projection * ModelView).invert());
		Vec4f screen_coords[3];
		for (int i = 0; i < model->nfaces(); i++) {
			for (int j = 0; j < 3; j++) {
				screen_coords[j] = shader.vertex(i, j);
			}
			triangle(screen_coords, shader, frame, zbuffer);
		}
		frame.flip_vertically(); // to place the origin in the bottom left corner of the image
		frame.write_tga_file("framebuffer.tga");
	}

	delete model;
	delete[] zbuffer;
	delete[] shadowbuffer;
	return 0;
}