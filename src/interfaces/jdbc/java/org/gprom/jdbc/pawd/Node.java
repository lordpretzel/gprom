/**
 * 
 */
package org.gprom.jdbc.pawd;
import java.text.SimpleDateFormat;
import java.util.Date;
/**
 * @author Amer
 *
 */
public class Node {
	
	

	/**
	 * @return the time
	 */

	public Date getTime() {
		return Time;
	}

	/**
	 * 
	 */
	String Id;
	boolean Materialized;
	String Description;
	Date Time;
	
	/**
	 * @return the id
	 */
	public String getId() {
		return Id;
	}

	/**
	 * @param id the id to set
	 */
	public void setId(String id) {
		Id = id;
	}

	/**
	 * @return the materialized
	 */
	public boolean isMaterialized() {
		return Materialized;
	}

	/**
	 * @param materialized the materialized to set
	 */
	public void setMaterialized(boolean materialized) {
		Materialized = materialized;
	}

	/**
	 * @return the description
	 */
	public String getDescription() {
		return Description;
	}

	/**
	 * @param description the description to set
	 */
	public void setDescription(String description) {
		Description = description;
	}

	public Node() {
		Id = VersionGraph.IDGenerator();
		Materialized = false ;
		Description = null ;
		Time = new Date();
	}
	public Node(boolean Materialized, String Description){
		Id= VersionGraph.IDGenerator();
		this.Materialized=Materialized;
		this.Description= Description;
		Time = new Date();
	}
	public Node( String ID, boolean Materialized, String Description,Date time){
		Id= ID;
		this.Materialized=Materialized;
		this.Description= Description;
		this.Time = time;
	}

	/* (non-Javadoc)
	 * @see java.lang.Object#toString()
	 */
	@Override
	public String toString() {
		SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ssZ");
		return "[Id=" + Id + 
				", Materialized=" + Materialized + 
				", Description=" + Description + 
				", Time=" + sdf.format(Time)+ "]\n";
	}


	

}