// Uses the actual triangulator; the exact-position edge checker is independent
// of the mesher's case table and normals. No position tolerance or face deletion.
#define main surface_cli_main
#include "../src/surface.cpp"
#undef main
#include <map>
#include <utility>

using Point=std::array<float,3>;
using Edge=std::pair<Point,Point>;

int main(){
    using namespace water;
    omp_set_num_threads(2);
    size_t cases=0,triangles=0,badEdges=0,empty=0,badSign=0;
    double smallestArea=std::numeric_limits<double>::infinity();
    for(int scale=0;scale<2;scale++)for(float ratio:{1e-2f,1e-6f,1e-12f,1e-20f})
        for(int adjacent=0;adjacent<2;adjacent++)for(int complement=0;complement<2;complement++){
            Reconstruction r(.001f,{});Reconstruction::FieldGrid g;
            g.nx=g.ny=g.nz=5;
            g.step=scale?.0053125f:.0006375f;
            g.origin=scale?V3(-.01f,.34f,-.51f):V3(.0612f,.00765f,.0082875f);
            g.phi.assign(125,complement?-1.f:1.f);g.foam.assign(125,0.f);
            g.phi[g.index(2,2,2)]=complement?ratio:-ratio;
            if(adjacent)g.phi[g.index(3,2,2)]=complement?ratio:-ratio;
            const auto mesh=r.triangulate(g);cases++;triangles+=mesh.size();if(mesh.empty())empty++;
            std::map<Edge,std::pair<int,int>> incidence;
            double volume=0;
            for(const auto&t:mesh){
                Point p[3]={{t.a.p.x,t.a.p.y,t.a.p.z},{t.b.p.x,t.b.p.y,t.b.p.z},{t.c.p.x,t.c.p.y,t.c.p.z}};
                double q[3][3];for(int i=0;i<3;i++)for(int a=0;a<3;a++)q[i][a]=double(p[i][a])-g.origin[a];
                double ax=q[1][0]-q[0][0],ay=q[1][1]-q[0][1],az=q[1][2]-q[0][2];
                double bx=q[2][0]-q[0][0],by=q[2][1]-q[0][1],bz=q[2][2]-q[0][2];
                double fx=ay*bz-az*by,fy=az*bx-ax*bz,fz=ax*by-ay*bx;
                smallestArea=std::min(smallestArea,.5*std::sqrt(fx*fx+fy*fy+fz*fz));
                volume+=(q[0][0]*(q[1][1]*q[2][2]-q[1][2]*q[2][1])+q[0][1]*(q[1][2]*q[2][0]-q[1][0]*q[2][2])+q[0][2]*(q[1][0]*q[2][1]-q[1][1]*q[2][0]))/6.;
                for(int i=0;i<3;i++){
                    Point a=p[i],b=p[(i+1)%3];int sign=1;if(b<a){std::swap(a,b);sign=-1;}
                    auto &edge=incidence[{a,b}];edge.first++;edge.second+=sign;
                }
            }
            for(const auto&kv:incidence)if(kv.first.first==kv.first.second||kv.second.first!=2||kv.second.second!=0)badEdges++;
            if(!(complement?volume<0:volume>0))badSign++;
        }
    bool passed=cases==32&&triangles>0&&badEdges==0&&empty==0&&badSign==0&&smallestArea>0;
    std::cout<<std::setprecision(17)<<"{\n  \"passed\": "<<(passed?"true":"false")<<",\n  \"cases\": "<<cases<<",\n  \"triangles\": "<<triangles<<",\n  \"bad_edges\": "<<badEdges<<",\n  \"empty_meshes\": "<<empty<<",\n  \"wrong_signed_volumes\": "<<badSign<<",\n  \"minimum_triangle_area_m2\": "<<smallestArea<<"\n}\n";
    return passed?0:1;
}
