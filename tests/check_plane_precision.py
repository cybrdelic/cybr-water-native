"""Compare the actual C++ fallback to 100-digit exact-input Decimal plane arithmetic."""
import argparse
from decimal import Decimal, localcontext
import json
import math
from pathlib import Path
import struct
import subprocess


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def oracle(case):
    with localcontext() as ctx:
        ctx.prec = 100
        a,b,c,o,d = [[Decimal.from_float(v) for v in row] for row in case]
        e = [b[i]-a[i] for i in range(3)]
        f = [c[i]-a[i] for i in range(3)]
        n = [e[(i+1)%3]*f[(i+2)%3]-e[(i+2)%3]*f[(i+1)%3] for i in range(3)]
        denominator = sum(n[i]*d[i] for i in range(3))
        if not denominator:return None
        return float(sum(n[i]*(a[i]-o[i]) for i in range(3))/denominator)


def main():
    parser=argparse.ArgumentParser();parser.add_argument("probe",type=Path);args=parser.parse_args()
    a=list(map(f32,[-.057374998927116394,.0006332624470815063,-.008929235860705376]))
    b=list(map(f32,[-.05673757568001747,.0006374226650223136,-.008925075642764568]))
    c=list(map(f32,[-.05673757568001747,.0006374226068146527,-.008924998342990875]))
    o=[-.057123413431534827,.00063490442876355596,-.0089275663640161311]
    d=list(map(f32,[.68056100606918335,.45588186383247375,.57359254360198975]))
    cases=[]
    for power in (-20,0,20):
        scale=2.0**power
        for axis in range(3):
            for step in (-1,0,1):
                origin=o.copy()
                if step:origin[axis]=math.nextafter(origin[axis],math.copysign(math.inf,step))
                for sign in (-1,1):
                    cases.append([[x*scale for x in v] for v in (a,b,c,origin)]+[[x*sign for x in d]])
    # Analytic planes: positive, negative, zero and parallel, with a sub-ULP segment.
    for z in (-2.0**-70,0.,2.0**-70):
        cases.append([[0.,0.,0.],[1.,0.,0.],[0.,1.,0.],[.25,.25,z],[0.,0.,-1.]])
    cases.append([[0.,0.,0.],[1.,0.,0.],[0.,1.,0.],[.25,.25,1.],[1.,0.,0.]])
    rows=[" ".join(format(x,".17g") for vector in case for x in vector) for case in cases]
    result=subprocess.run([str(args.probe.resolve())],input="\n".join(rows)+"\n",capture_output=True,text=True,check=True,timeout=30)
    values=[float(value) for value in result.stdout.splitlines()]
    if len(values)!=len(cases):raise AssertionError("Incomplete C++ probe output")
    max_relative=0.
    for i,(case,value) in enumerate(zip(cases,values)):
        expected=oracle(case)
        if expected is None:
            if not math.isnan(value):raise AssertionError(f"Parallel plane {i} did not reject")
        elif expected==0:
            if value!=0:raise AssertionError(f"Exact plane {i} moved off zero")
        else:
            if not math.isfinite(value) or (value>0)!=(expected>0):raise AssertionError(f"Wrong sign at {i}")
            relative=abs((value-expected)/expected);max_relative=max(max_relative,relative)
            if relative>1e-12:raise AssertionError(f"Plane {i}: {value} vs {expected} relative={relative}")
    print(json.dumps({"passed":True,"cases":len(cases),"max_relative_error":max_relative,
                      "scope":"Near-origin signs, adjacent Float64 origins, powers-of-two scale, exact/parallel planes; Decimal100 oracle"}))


if __name__=="__main__":main()
