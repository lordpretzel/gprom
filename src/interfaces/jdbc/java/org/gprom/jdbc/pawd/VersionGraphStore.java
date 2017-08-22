/**
 * 
 */
package org.gprom.jdbc.pawd;



import java.util.ArrayList;
import java.util.Map;

import org.gprom.jdbc.pawd.VersionGraphStore.Operation.Materialization;
import org.json.JSONObject;
/**
 * @author Amer
 *
 */
public interface VersionGraphStore {

	/**
	 * 
	 */

	public VersionGraph Load(JSONObject GraphJSONArray);
	public void UpdateCall(VersionGraph V, Node s);
	public JSONObject Save(VersionGraph V);
	public String Compose(VersionGraph V, Node s, Map<Node,Materialization> MaterializationPlan);
	public ArrayList<VersionGraph> getPath(VersionGraph V,Node n);
	//public VersionGraph genPathes(ArrayList<VersionGraph>Graphs,VersionGraph V,Node t);
	//store as a JSON object
	public void Configure(VersionGraph V);
	//method for saving configuration
	public class Operation {
		String Code;
		OpType Op;
		public Operation(String code, OpType op) {
			Code = code;
			Op = op;
		}
		/**
		 * @return the code
		 */
		public String getCode() {
			return Code;
		}
		/**
		 * @param code the code to set
		 */
		public void setCode(String code) {
			Code = code;
		}
		/**
		 * @return the op
		 */
		public OpType getOp() {
			return Op;
		}
		/**
		 * @param op the op to set
		 */
		public void setOp(OpType op) {
			Op = op;
		}
		/* (non-Javadoc)
		 * @see java.lang.Object#toString()
		 */
		@Override
		public String toString() {
			return "[Code=" + Code + ", Op=" + Op + "]";
		}

		public enum OpType{
			Query,
			Update
		}
		public enum Materialization{
			isMaterialized,
			notMaterialized
		}
		@Override 
		public boolean equals(Object other){
		    if (other == null) return false;
		    if (other == this) return true;
		    if (!(other instanceof Operation))return false;
		    Operation otherOp = (Operation)other;
		    if(otherOp.getCode().equals(Code) && otherOp.Op.equals(Op)) return true;
		    return false;
		}

	}

}
