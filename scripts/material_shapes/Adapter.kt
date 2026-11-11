/*
 * Copyright 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// JVM-only value adapters for the Compose math used by MaterialShapes.
// Derived from the pinned Offset.kt, Matrix.kt and ShapeUtil.kt snapshots.
// Only 2D rotation/scaling are needed; RoundedPolygon and all cubic geometry
// are evaluated by the unmodified AndroidX graphics-shapes library.
import androidx.graphics.shapes.*
import kotlin.math.*
data class Offset(val x: Float, val y: Float) {
 operator fun plus(o: Offset) = Offset(x+o.x,y+o.y)
 operator fun minus(o: Offset) = Offset(x-o.x,y-o.y)
 operator fun times(v: Float) = Offset(x*v,y*v)
 fun getDistance() = sqrt(x*x+y*y)
 companion object { val Zero=Offset(0f,0f) }
}
class Matrix {
 private val v= floatArrayOf(1f,0f,0f,1f)
 fun rotateZ(degrees: Float) {
  val r=degrees*(PI/180.0); val s=sin(r).toFloat(); val c=cos(r).toFloat()
  val a00=v[0]; val a10=v[2]; val a01=v[1]; val a11=v[3]
  v[0]=c*a00+s*a10; v[2]=-s*a00+c*a10
  v[1]=c*a01+s*a11; v[3]=-s*a01+c*a11
 }
 fun scale(x: Float=1f,y: Float=1f) {v[0]*=x;v[1]*=x;v[2]*=y;v[3]*=y}
 fun map(x:Float,y:Float)=TransformResult(v[0]*x+v[2]*y+0f,v[1]*x+v[3]*y+0f)
}
fun RoundedPolygon.transformed(matrix:Matrix)=transformed { x,y -> matrix.map(x,y) }
inline fun <T,R> List<T>.fastMap(block:(T)->R):List<R> = map(block)
