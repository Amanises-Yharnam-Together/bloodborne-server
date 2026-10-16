"""Keying helpers for scripts/art/make_art.py."""
import numpy as np
from PIL import Image, ImageFilter
def F(arr, flt):
    return np.asarray(Image.fromarray(np.clip(arr,0,255).astype(np.uint8)).filter(flt)).astype(float)
def ss(x,a,b): t=np.clip((x-a)/(b-a),0,1); return t*t*(3-2*t)
def key(O, lo, dark, cell, soft_gain=0.85, sat_solid=45, hard=True, tint=(110,125,150)):
    L=O.mean(-1); sat=O.max(-1)-O.min(-1)
    thr=lo-10
    ah=np.clip((thr-L)/(thr-dark),0,1)
    solid=ss(ah,0.55,0.9)
    solid=np.maximum(solid, ss(sat,sat_solid,sat_solid+25))
    if not hard: solid=solid*0
    k=cell|1
    Lm=F(F(L,ImageFilter.MinFilter(k)),ImageFilter.GaussianBlur(cell*0.8))
    asoft=np.clip((thr-Lm)/(thr-dark),0,1)*soft_gain
    # soft colour: un-mix the min-filtered colour against the low checker level
    Cs=np.empty_like(O)
    for c in range(3):
        m=F(F(O[...,c],ImageFilter.MinFilter(k)),ImageFilter.GaussianBlur(cell*0.8))
        aa=np.maximum(asoft/max(soft_gain,1e-3),0.08)
        un=np.clip((m-(1-aa)*lo)/aa,0,255)
        t=ss(aa,0.15,0.5)
        Cs[...,c]=tint[c]*(1-t)+un*t
    a=np.maximum(solid,asoft)
    w=np.where(a>0,solid/np.maximum(a,1e-3),0)[...,None]
    C=np.nan_to_num(w*O+(1-w)*Cs)
    a[a<0.02]=0
    return C,a
def save(C,a,path,bgprev=None):
    im=Image.fromarray(np.dstack([np.clip(C,0,255).astype(np.uint8),np.clip(a*255,0,255).astype(np.uint8)]),"RGBA")
    im.save(path)
    if bgprev:
        bg=Image.new("RGBA",im.size,(13,15,19,255)); bg.alpha_composite(im); bg.convert("RGB").save(bgprev,quality=90)
    return im
